/*
 * clsic-vox.c -- ALSA SoC CLSIC VOX
 *
 * Copyright (C) 2015-2018 Cirrus Logic, Inc. and
 *			   Cirrus Logic International Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/kthread.h>
#include <linux/firmware.h>

#include <sound/core.h>
#include <sound/compress_driver.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <linux/mfd/tacna/core.h>
#include <linux/mfd/tacna/registers.h>
#include "tacna.h"

#include "../../../drivers/mfd/clsic/clsic-trace.h"
#include <linux/mfd/clsic/core.h>
#include "clsic-vox-msg.h"
#include "clsic-vox-trace.h"
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>

#include "clsic-vox.h"

/**
 * size_of_bio_results() - get the size of the biometric results struct in use
 * @bio_results_format:	The format of biometric results struct whose size is
 *			being queried.
 *
 * Return: The size of the struct corresponding to the passed-in format.
 */
static inline int size_of_bio_results(uint8_t bio_results_format)
{
	switch (bio_results_format) {
	case VOX_BIO_RESULTS_CLASSIC:
		return sizeof(struct clsic_vox_auth_result);
	case VOX_BIO_RESULTS_EXT_V1:
		return sizeof(struct clsic_vox_auth_result_ex);
	case VOX_BIO_RESULTS_EXT_V2:
		return sizeof(struct clsic_vox_auth_result_ex2);
	default:
		return 0;
	}
}

static int vox_set_mode(struct clsic_vox *vox, enum clsic_vox_mode new_mode);
static int vox_update_barge_in(struct clsic_vox *vox);
static void vox_set_idle_and_state(struct clsic_vox *vox,
				   bool set_clsic_to_idle,
				   int drv_state);

/**
 * vox_send_userspace_event() - notify userspace of a change.
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Notify userspace that a change has happened using an event.
 *
 */
static void vox_send_userspace_event(struct clsic_vox *vox)
{
	snd_ctl_notify(vox->codec->component.card->snd_card,
		       SNDRV_CTL_EVENT_MASK_VALUE, &vox->error_info_kctrl->id);
}

/**
 * clsic_vox_asr_end_streaming() - set CLSIC back to IDLE after a problem.
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * When the audio path has closed, the last operation to be running needs to
 * clear up.
 *
 */
static void clsic_vox_asr_end_streaming(struct clsic_vox *vox)
{
	vox->trigger_phrase_id = VOX_TRGR_INVALID;
	vox->trigger_engine_id = VOX_TRGR_INVALID;

	vox->scc_cap_preamble_ms = 0;
	vox->scc_status &= (~VTE1_ACTIVE);

	vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
}

/**
 * clsic_vox_asr_cleanup_states() - ensure that other threads close sensibly
 *				when there is no more ASR streaming to be done.
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Use the driver state to determine how to bring down the ASR operations.
 *
 */
static void clsic_vox_asr_cleanup_states(struct clsic_vox *vox)
{
	vox->scc_status = 0;

	clsic_vox_asr_end_streaming(vox);

	vox->asr_stream.listen_error = true;
	complete(&vox->asr_stream.completion);
	complete(&vox->new_bio_results_completion);
}

/**
 * clsic_vox_asr_stream_open() - open the ASR stream
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 *
 * Standard .open function - see struct snd_compr_ops for more details.
 *
 * Return: errno.
 */
static int clsic_vox_asr_stream_open(struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(rtd->codec);
	struct clsic *clsic = vox->clsic;
	int ret = 0;

	/*
	 * When the ASR stream is open we must prevent the sound card and the
	 * core CLSIC drivers from being unloaded as this would make the
	 * callback function pointers invalid.
	 *
	 * Attempt to get a reference count on the required driver modules,
	 * these calls may fail if the module is already being unloaded.
	 */
	if (!try_module_get(vox->codec->component.card->owner))
		return -EBUSY;

	if (!try_module_get(clsic->dev->driver->owner)) {
		module_put(vox->codec->component.card->owner);
		return -EBUSY;
	}

	if (strcmp(rtd->codec_dai->name, "clsic-dsp-vox-asr") != 0) {
		clsic_err(vox->clsic,
			  "No compressed stream supported for: %s\n",
			  rtd->codec_dai->name);
		ret = -EINVAL;
		goto error_return;
	}

	if (vox->asr_stream.stream) {
		clsic_err(vox->clsic, "ASR stream already active.\n");
		ret = -EBUSY;
		goto error_return;
	}

	if (stream->direction != SND_COMPRESS_CAPTURE) {
		clsic_err(vox->clsic,
			  "Only capture is supported for ASR stream.\n");
		ret = -EINVAL;
		goto error_return;
	}

	pm_runtime_get_sync(clsic->dev);

	vox->asr_stream.stream = stream;

	stream->runtime->private_data = &vox->asr_stream;

	trace_clsic_vox_asr_stream_open(stream->direction);

	return 0;

error_return:
	module_put(vox->clsic->dev->driver->owner);
	module_put(vox->codec->component.card->owner);
	return ret;
}

/**
 * clsic_vox_asr_stream_free() - close the ASR stream
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 *
 * Standard .free function - see struct snd_compr_ops for more details.
 *
 * Return: 0 always.
 */
static int clsic_vox_asr_stream_free(struct snd_compr_stream *stream)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox = container_of(asr_stream, struct clsic_vox,
					     asr_stream);
	struct clsic *clsic = vox->clsic;

	trace_clsic_vox_asr_stream_free(stream->direction,
					asr_stream->copied_total);

	clsic_vox_asr_cleanup_states(vox);

	kfree(asr_stream->buf.data);

	asr_stream->buf.data = NULL;
	asr_stream->buf.size = 0;
	asr_stream->buf.frag_sz = 0;

	mutex_lock(&asr_stream->stream_lock);
	asr_stream->stream = NULL;
	mutex_unlock(&asr_stream->stream_lock);

	pm_runtime_put_autosuspend(clsic->dev);
	module_put(clsic->dev->driver->owner);
	module_put(vox->codec->component.card->owner);

	return 0;
}

/**
 * clsic_vox_asr_stream_block_sz() - close the ASR stream
 * @block_size:	size of the ASR block in bytes.
 *
 * Convert an actual ASR block size in bytes into the enumeration used by the
 * CLSIC messaging protocol.
 *
 * Return: CLSIC enumerated code representing block size.
 */
static int clsic_vox_asr_stream_block_sz(u32 block_size)
{
	switch (block_size) {
	case 192:
		return CLSIC_VOX_ASR_BLK_SZ_192;
	case 384:
		return CLSIC_VOX_ASR_BLK_SZ_384;
	case 960:
		return CLSIC_VOX_ASR_BLK_SZ_960;
	case 1920:
		return CLSIC_VOX_ASR_BLK_SZ_1920;
	case 4800:
		return CLSIC_VOX_ASR_BLK_SZ_4800;
	case 9600:
		return CLSIC_VOX_ASR_BLK_SZ_9600;
	default:
		return -EINVAL;
	}
}

#define PCM_S16_LE_BYTES_PER_SAMPLE 2

/**
 * clsic_vox_asr_stream_set_params() - set up internal ASR parameters
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 * @params:	Standard parameter as used by compressed stream infrastructure.
 *
 * Standard .set_params function - see struct snd_compr_ops for more details.
 *
 * Return: errno.
 */
static int clsic_vox_asr_stream_set_params(struct snd_compr_stream *stream,
					   struct snd_compr_params *params)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	size_t frag_sz = params->buffer.fragment_size;
	int block_sz, i, frame_sz;

	frame_sz = params->codec.ch_in * PCM_S16_LE_BYTES_PER_SAMPLE;
	if (frag_sz % frame_sz) {
		clsic_err(clsic,
			  "requested ASR stream fragment size %u is not supported (frame size %u).\n",
			  frag_sz, frame_sz);
		return -EINVAL;
	}
	block_sz = clsic_vox_asr_stream_block_sz(frag_sz / frame_sz);
	if (block_sz < 0) {
		clsic_err(clsic,
			  "requested ASR stream fragment size %u is not supported (frame size %u).\n",
			  frag_sz, frame_sz);
		return -EINVAL;
	}

	if (clsic_asr_stream_caps.id != params->codec.id) {
		clsic_err(clsic,
			  "requested codec ID (%u) is not correct (%u).\n",
			  params->codec.id, clsic_asr_stream_caps.id);
		return -EINVAL;
	}

	if (clsic_asr_stream_caps.desc.max_ch != params->codec.ch_in) {
		clsic_err(clsic,
			  "requested ch_in (%u) is not supported number of channels (%u).\n",
			  params->codec.ch_in,
			  clsic_asr_stream_caps.desc.max_ch);
		return -EINVAL;
	}

	if (!(clsic_asr_stream_caps.desc.formats
	      & (1 << params->codec.format))) {
		clsic_err(clsic,
			  "requested format bit %d is not supported (format bitmask: 0x%x).\n",
			  params->codec.format,
			  clsic_asr_stream_caps.desc.formats);
		return -EINVAL;
	}

	for (i = 0; i < clsic_asr_stream_caps.desc.num_sample_rates; ++i)
		if (clsic_asr_stream_caps.desc.sample_rates[i] ==
				params->codec.sample_rate)
			break;

	if (i >= clsic_asr_stream_caps.desc.num_sample_rates) {
		clsic_err(clsic,
			  "requested sample rate (%u) is not supported.\n",
			  params->codec.sample_rate);
		return -EINVAL;
	}

	asr_stream->block_sz = block_sz;
	/* Avoid memory leaks from multiple calls to set_params. */
	kfree(asr_stream->buf.data);
	asr_stream->buf.data = kmalloc(frag_sz, GFP_KERNEL);
	if (!asr_stream->buf.data)
		return -ENOMEM;
	asr_stream->buf.size = frag_sz;

	trace_clsic_vox_asr_stream_set_params(params, frag_sz);

	return 0;
}

/**
 * clsic_vox_asr_stream_data_cb() - asynchronous message callback for ASR
 * @clsic:	The main shared instance of struct clsic used in the CLSIC
 *		drivers.
 * @msg:	The message notification itself as received from CLSIC.
 *
 * This is the callback that is called when the asynchronous message to copy
 * ASR data during streaming has completed.
 *
 * Return: CLSIC_HANDLED or CLSIC_UNHANDLED.
 */
static enum clsic_message_cb_ret clsic_vox_asr_stream_data_cb(
						      struct clsic *clsic,
						      struct clsic_message *msg)
{
	struct clsic_vox *vox = (struct clsic_vox *) (uintptr_t) msg->cookie;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;
	union clsic_vox_msg *msg_rsp;
	u32 payload_sz = 0;

	if (msg->state != CLSIC_MSG_SUCCESS) {
		clsic_err(clsic, "async message failed with state: %d\n",
			  msg->state);
		asr_stream->cb_error = true;
	} else {
		msg_rsp = (union clsic_vox_msg *) &msg->response;
		if (!clsic_get_bulk_bit(msg_rsp->rsp_get_asr_block.hdr.sbc) &&
		    (msg_rsp->rsp_get_asr_block.hdr.err != 0)) {
			/*
			 * We have set CLSIC to IDLE mode while there is a
			 * pending ASR request. This causes CLSIC to forcibly
			 * cancel the request for that ASR block.
			 */
			clsic_dbg(clsic, "response: %d\n",
				  msg_rsp->rsp_get_asr_block.hdr.err);
			asr_stream->cb_error = true;
		} else if (msg_rsp->blkrsp_get_asr_block.hdr.err != 0) {
			clsic_dbg(clsic, "bulk response: %d\n",
				  msg_rsp->blkrsp_get_asr_block.hdr.err);
			asr_stream->cb_error = true;
		} else {
			payload_sz = msg_rsp->blkrsp_get_asr_block.hdr.bulk_sz;
			asr_stream->copied_total += payload_sz;

			/* Alert userspace via compressed framework. */
			mutex_lock(&asr_stream->stream_lock);
			if (asr_stream->stream)
				snd_compr_fragment_elapsed(asr_stream->stream);
			mutex_unlock(&asr_stream->stream_lock);
		}
	}

	trace_clsic_vox_asr_stream_data_rcv(payload_sz);

	pm_runtime_put_autosuspend(clsic->dev);
	module_put(vox->codec->dev->driver->owner);

	return CLSIC_MSG_RELEASED;
}

/**
 * clsic_vox_asr_queue_async() - helper function for copying ASR data
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * This function handles creating and sending an asynchronous message to copy
 * ASR data during streaming. It also sensibly reacts if the message sending
 * fails.
 *
 * Return: errno.
 */
static int clsic_vox_asr_queue_async(struct clsic_vox *vox)
{
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;
	int ret;

	/*
	 * when an async message is outstanding the vox driver cannot be
	 * unloaded as this would make the callback function invalid
	 */
	if (!try_module_get(vox->codec->dev->driver->owner))
		return -EBUSY;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);
	pm_runtime_get_sync(clsic->dev);
	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   (uint8_t *) asr_stream->buf.data,
				   asr_stream->buf.size,
				   (uint64_t) (uintptr_t) vox,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		clsic_err(clsic, "Error sending msg: %d\n", ret);

		clsic_vox_asr_end_streaming(vox);

		/* Alert userspace via compressed framework. */
		mutex_lock(&asr_stream->stream_lock);
		if (asr_stream->stream)
			snd_compr_fragment_elapsed(asr_stream->stream);
		mutex_unlock(&asr_stream->stream_lock);

		pm_runtime_put_autosuspend(clsic->dev);
		module_put(vox->codec->dev->driver->owner);
		return -EINVAL;
	}

	return 0;
}

/**
 * clsic_vox_asr_stream_wait_for_trigger() - wait for initial CLSIC trigger
 * @data:	struct clsic_asr_stream data.
 *
 * CLSIC will notify this driver when a trigger has occurred and the aim here is
 * to react to this trigger by starting to get ASR blocks from CLSIC.
 *
 * Return: errno.
 */
static int clsic_vox_asr_stream_wait_for_trigger(void *data)
{
	struct clsic_asr_stream *asr_stream = data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	struct clsic_vox_trgr_info trgr_info;
	int ret = 0;

	ret = wait_for_completion_interruptible(&asr_stream->completion);
	if (ret || asr_stream->listen_error) {
		clsic_dbg(clsic, "Wait for ASR stream trigger aborted.\n");

		/* Alert userspace via compressed framework. */
		mutex_lock(&asr_stream->stream_lock);
		if (asr_stream->stream)
			snd_compr_fragment_elapsed(asr_stream->stream);
		mutex_unlock(&asr_stream->stream_lock);

		clsic_vox_asr_end_streaming(vox);

		return -EIO;
	}

	trace_clsic_vox_asr_stream_data_start(asr_stream->copied_total);

	mutex_lock(&vox->drv_state_lock);
	if (vox->drv_state == VOX_DRV_STATE_LISTENING) {
		vox->drv_state = VOX_DRV_STATE_STREAMING;

		vox->scc_status &= (~VTE1_ACTIVE);
		vox->scc_status |= VTE1_TRIGGERED_SINCE_LISTEN;
		vox->scc_status |= VTE1_TRIGGERED_MOST_RECENT;

		mutex_unlock(&vox->drv_state_lock);
	} else {
		mutex_unlock(&vox->drv_state_lock);
		return -EINVAL;
	}

	/* Fill in the trigger information. */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_GET_TRGR_INFO);
	ret = clsic_send_msg_sync_pm(clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  (uint8_t *) &trgr_info,
				  sizeof(struct clsic_vox_trgr_info));
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);

		clsic_vox_asr_end_streaming(vox);

		return -EIO;
	}

	/* Response is either bulk in case of success, or not. */
	if (!clsic_get_bulk_bit(msg_rsp.rsp_get_trgr_info.hdr.sbc)) {
		clsic_err(vox->clsic, "failure %d.\n",
			  msg_rsp.rsp_get_trgr_info.hdr.err);
		clsic_vox_asr_end_streaming(vox);

		return -EIO;
	}

#ifdef CONFIG_DEBUG_FS
	/* Save the trigger data for later analysis */
	memcpy(&vox->last_trigger.msg, &msg_rsp, sizeof(union clsic_vox_msg));
	memcpy(&vox->last_trigger.info, &trgr_info,
	       sizeof(struct clsic_vox_trgr_info));
	vox->last_trigger.blob.size = sizeof(vox->last_trigger.msg) +
		sizeof(vox->last_trigger.info);
#endif

	/* Populate the ALSA controls with the trigger information. */
	vox->trigger_engine_id = trgr_info.engineid;
	vox->trigger_phrase_id = trgr_info.phraseid;

	asr_stream->cb_error = false;

	trace_clsic_vox_asr_stream_queue_read(asr_stream->copied_total);

	/* Queue up the first read. */
	if (clsic_vox_asr_queue_async(vox))
		return -EIO;

	return 0;
}

/**
 * clsic_vox_asr_stream_trigger() - respond to userspace
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 * @cmd:	A start or stop flag for compressed audio streaming.
 *
 * Standard .trigger function - see struct snd_compr_ops for more details. When
 * userspace (crec) starts reading an active compressed stream of audio, this
 * function is called with a relevant command regarding whether the stream has
 * just started or just stopped.
 *
 * Return: errno.
 */
static int clsic_vox_asr_stream_trigger(struct snd_compr_stream *stream,
					int cmd)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		if (asr_stream->buf.size == 0) {
			/* Last of the params to be set in set_params. */
			clsic_dbg(clsic, "Bad ASR params. Unable to start.\n");
			return -EIO;
		}

		/* Fail if any ongoing vox operations. */
		mutex_lock(&vox->drv_state_lock);
		if (vox->drv_state == VOX_DRV_STATE_NEUTRAL) {
			vox->drv_state = VOX_DRV_STATE_LISTENING;
			mutex_unlock(&vox->drv_state_lock);
		} else {
			mutex_unlock(&vox->drv_state_lock);
			clsic_dbg(clsic,
				  "Audio path opened with bad state %d.\n",
				  vox->drv_state);
			return -EIO;
		}

		vox_update_barge_in(vox);

		ret = vox_set_mode(vox, CLSIC_VOX_MODE_LISTEN);
		if (ret)
			return ret;

		reinit_completion(&asr_stream->completion);
		reinit_completion(&vox->new_bio_results_completion);
		vox->auth_error = CLSIC_ERR_NONE;

		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_LISTEN_START);
		msg_cmd.cmd_listen_start.trgr_domain =
						CLSIC_VOX_TRIG_DOMAIN_INTRNL;
		msg_cmd.cmd_listen_start.asr_blk_sz = asr_stream->block_sz;
		pm_runtime_get_sync(clsic->dev);
		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			pm_runtime_put_autosuspend(clsic->dev);
			break;
		} else if (msg_rsp.rsp_listen_start.hdr.err) {
			clsic_dbg(clsic,
				  "Failed to start listening: %d\n",
				  msg_rsp.rsp_listen_start.hdr.err);
			ret = -EINVAL;
			pm_runtime_put_autosuspend(clsic->dev);
			break;
		}

		trace_clsic_vox_asr_stream_listen(
					msg_cmd.cmd_listen_start.trgr_domain);

		vox->scc_status |= VTE1_ACTIVE;

		asr_stream->listen_error = false;
		asr_stream->copied_total = 0;
		asr_stream->wait_for_trigger =
			kthread_create(clsic_vox_asr_stream_wait_for_trigger,
				       asr_stream,
				       "clsic-vox-asr-wait-for-trigger");

		wake_up_process(asr_stream->wait_for_trigger);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
		clsic_vox_asr_cleanup_states(vox);
		pm_runtime_put_autosuspend(clsic->dev);
		break;
	default:
		return -EINVAL;
	}

	if (ret)
		clsic_vox_asr_end_streaming(vox);

	return ret;
}

/**
 * clsic_vox_asr_stream_pointer() - get timestamp information about the
 *					ASR stream
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 * @tstamp:	Standard parameter as used by compressed stream infrastructure.
 *
 * Standard .pointer function - see struct snd_compr_ops for more details.
 *
 * Return: 0 always.
 */
static int clsic_vox_asr_stream_pointer(struct snd_compr_stream *stream,
					struct snd_compr_tstamp *tstamp)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;

	tstamp->copied_total = asr_stream->copied_total;
	tstamp->sampling_rate = asr_stream->sample_rate;

	return 0;
}

/**
 * clsic_vox_asr_stream_copy() - copy ASR data to userspace buffer
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 * @buf:	Userspace buffer to copy compressed data to.
 * @count:	How many bytes to copy to userspace.
 *
 * Standard .copy function - see struct snd_compr_ops for more details.
 * Basically starts another read of a block of ASR data from CLSIC.
 *
 * Return: errno.
 */
static int clsic_vox_asr_stream_copy(struct snd_compr_stream *stream,
				     char __user *buf,
				     size_t count)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;

	if ((vox->drv_state != VOX_DRV_STATE_STREAMING) &&
	    (vox->drv_state != VOX_DRV_STATE_GETTING_BIO_RESULTS)) {
		clsic_err(clsic, "ASR not streaming yet.\n");
		return -EINVAL;
	}

	if (asr_stream->cb_error) {
		clsic_vox_asr_end_streaming(vox);
		return -EFAULT;
	}

	count = min(count, asr_stream->buf.size);

	trace_clsic_vox_asr_stream_copy_start(count);

	if (copy_to_user(buf, (uint8_t *) asr_stream->buf.data, count)) {
		clsic_err(clsic, "Failed to copy data to user.\n");
		clsic_vox_asr_end_streaming(vox);
		return -EFAULT;
	}

	trace_clsic_vox_asr_stream_copy_end(count, asr_stream->copied_total);

	/* Queue up next read. */
	if (clsic_vox_asr_queue_async(vox))
		return -EIO;

	return count;
}

/**
 * clsic_vox_asr_stream_get_caps() - copy ASR data to userspace buffer
 * @stream:	Standard parameter as used by compressed stream infrastructure.
 * @caps:	Standard parameter as used by compressed stream infrastructure.
 *
 * Standard .get_caps function - see struct snd_compr_ops for more details.
 *
 * Return: 0 always.
 */
static int clsic_vox_asr_stream_get_caps(struct snd_compr_stream *stream,
					 struct snd_compr_caps *caps)
{
	caps->codecs[0] = clsic_asr_stream_caps.id;
	caps->direction = SND_COMPRESS_CAPTURE;
	caps->min_fragment_size = VOX_ASR_MIN_FRAGMENT_SZ;
	caps->max_fragment_size = VOX_ASR_MAX_FRAGMENT_SZ;
	caps->min_fragments = VOX_ASR_MIN_FRAGMENTS;
	caps->max_fragments = VOX_ASR_MAX_FRAGMENTS;

	return 0;
}

static struct snd_soc_dai_driver clsic_vox_dai[] = {
	{
		.name = "clsic-cpu-vox-asr",
		.capture = {
			.stream_name = "VOX ASR CPU",
			.channels_min = 2,
			.channels_max = 2,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
		.compress_new = snd_soc_new_compress,
	},
	{
		.name = "clsic-dsp-vox-asr",
		.capture = {
			.stream_name = "VOX ASR DSP",
			.channels_min = 2,
			.channels_max = 2,
			.rates = TACNA_RATES,
			.formats = TACNA_FORMATS,
		},
	},
};

static const struct snd_compr_ops clsic_vox_compr_ops = {
	.open = clsic_vox_asr_stream_open,
	.free = clsic_vox_asr_stream_free,
	.set_params = clsic_vox_asr_stream_set_params,
	.trigger = clsic_vox_asr_stream_trigger,
	.pointer = clsic_vox_asr_stream_pointer,
	.copy = clsic_vox_asr_stream_copy,
	.get_caps = clsic_vox_asr_stream_get_caps,
};

static const struct snd_soc_platform_driver clsic_vox_compr_platform = {
	.compr_ops = &clsic_vox_compr_ops,
};

/**
 * vox_set_pm_from_mode() - set power management options using the CLSIC mode
 * @vox:	The main instance of struct clsic_vox used in this driver.
 * @new_mode:	New CLSIC mode to change to.
 *
 * Mark CLSIC as in use dependent on what CLSIC mode transition is occurring.
 *
 */
static inline void vox_set_pm_from_mode(struct clsic_vox *vox,
					enum clsic_vox_mode new_mode)
{
	if ((new_mode == CLSIC_VOX_MODE_IDLE) ||
	    (new_mode == CLSIC_VOX_MODE_LISTEN)) {
		if ((vox->clsic_mode != CLSIC_VOX_MODE_IDLE) &&
		    (vox->clsic_mode != CLSIC_VOX_MODE_LISTEN)) {
			clsic_msgproc_release(vox->clsic,
					vox->service->service_instance);
		}
	} else {
		clsic_msgproc_use(vox->clsic,
				  vox->service->service_instance);
	}
}

/**
 * vox_set_mode() - set mode on CLSIC
 * @vox:	The main instance of struct clsic_vox used in this driver.
 * @new_mode:	New CLSIC mode to change to.
 *
 * Set the mode on CLSIC. Includes inter-operation with the power management
 * infrastructure to provide hints as to when CLSIC should power down based on
 * the new mode demanded.
 *
 * Return: errno.
 */
static int vox_set_mode(struct clsic_vox *vox, enum clsic_vox_mode new_mode)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_set_mode(vox->clsic_mode, new_mode);

	if (vox->clsic_mode == new_mode)
		return 0;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_SET_MODE);
	msg_cmd.cmd_set_mode.mode = new_mode;

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	if (msg_rsp.rsp_set_mode.hdr.err != CLSIC_ERR_NONE) {
		vox->clsic_error_code = msg_rsp.rsp_set_mode.hdr.err;
		return -EINVAL;
	}

	vox_set_pm_from_mode(vox, new_mode);
	vox->clsic_mode = new_mode;

	return 0;
}

/**
 * vox_set_idle_and_state() - set CLSIC to IDLE mode and set driver management
 *			     mode
 * @vox:	The main instance of struct clsic_vox used in this driver.
 * @set_clsic_to_idle:	Whether to set CLSIC to IDLE mode or not.
 * @drv_state:	New vox driver state to change to.
 *
 * This function incorporates the 3 commonly performed tasks of setting CLSIC to
 * IDLE mode, setting the internal driver state and then notifying userspace
 * (i.e. waking the poll) that something has changed (usually meant to imply
 * that the error control node has changed value).
 *
 */
static void vox_set_idle_and_state(struct clsic_vox *vox,
				   bool set_clsic_to_idle, int drv_state)
{
	int ret = 0;

	trace_clsic_vox_set_idle_and_state(set_clsic_to_idle, drv_state);

	if (set_clsic_to_idle) {
		ret = vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);
		if (ret) {
			clsic_err(vox->clsic,
				  "unable to change to driver state %d from %d (ret = %d, CLSIC error code %d).\n",
				  drv_state,
				  vox->drv_state,
				  ret,
				  vox->clsic_error_code);
			return;
		}
	}

	vox->drv_state = drv_state;
}

/**
 * vox_update_phrases() - update internal cache of biometric phrase
 *			  installation states
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Query CLSIC to find out which biometric phrases are installed.
 *
 * Return: errno.
 */
static int vox_update_phrases(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret, phr;

	/* Phrases. */
	for (phr = 0; phr < VOX_MAX_PHRASES; phr++) {
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_IS_PHRASE_INSTALLED);
		msg_cmd.cmd_is_phrase_installed.phraseid = phr;

		ret = clsic_send_msg_sync_pm(
				     vox->clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
			return -EIO;
		}

		switch (msg_rsp.rsp_is_phrase_installed.hdr.err) {
		case CLSIC_ERR_NONE:
			vox->phrase_installed[phr] = true;
			break;
		case CLSIC_ERR_PHRASE_NOT_INSTALLED:
			vox->phrase_installed[phr] = false;
			break;
		default:
			clsic_err(vox->clsic,
				  "failed to check if phrase %d was installed %d.\n",
				  phr, msg_rsp.rsp_is_phrase_installed.hdr.err);
			return -EIO;
		}
	}

	return 0;
}

/**
 * vox_update_bins() - update internal cache of VTE/SSF bin installation states
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Query CLSIC to find out which VTE/SSF bin files are installed.
 *
 * Return: errno.
 */
static int vox_update_bins(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret, bin;

	/* VTE bins. */
	for (bin = 0; bin < CLSIC_VOX_BIN_CNT; bin++) {
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_IS_BIN_INSTALLED);
		msg_cmd.cmd_is_bin_installed.binid = bin;

		ret = clsic_send_msg_sync_pm(
				     vox->clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
			return -EIO;
		}

		switch (msg_rsp.rsp_is_bin_installed.hdr.err) {
		case CLSIC_ERR_NONE:
			vox->bin_installed[bin] = true;
			break;
		case CLSIC_ERR_BIN_NOT_INSTALLED:
			vox->bin_installed[bin] = false;
			break;
		default:
			clsic_err(vox->clsic,
				  "failed to check VTE bin status %d.\n",
				  msg_rsp.rsp_is_bin_installed.hdr.err);
			return -EIO;
		}
	}

	return 0;
}

/**
 * vox_update_map() - update internal cache of map installation state
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Query CLSIC to find out whether a map file is installed.
 *
 * Return: errno.
 */
static int vox_update_map(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	/* Map between VTE and biometric phrase. */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_IS_BIOVTE_MAP_INSTALLED);
	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	switch (msg_rsp.rsp_is_biovte_map_installed.hdr.err) {
	case CLSIC_ERR_NONE:
		vox->bio_vte_map_installed = true;
		break;
	case CLSIC_ERR_BIOVTE_MAP_NOT_INSTALLED:
		vox->bio_vte_map_installed = false;
		break;
	default:
		clsic_err(vox->clsic,
			  "failed to check biometrics VTE map status %d.\n",
			  msg_rsp.rsp_is_biovte_map_installed.hdr.err);
		return -EIO;
	}

	return 0;
}

/**
 * vox_update_assets_status() - update all internal asset states
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Update the cached states of all assets.
 *
 * Return: errno.
 */
static int vox_update_assets_status(struct clsic_vox *vox)
{
	int ret;

	ret = vox_update_phrases(vox);
	if (ret)
		return ret;

	if (vox->service->service_version <= CLSIC_VOX_SRV_VERSION_MVP2)
		/* MVP2.0 nothing else to update. */
		return 0;

	ret = vox_update_bins(vox);
	if (ret)
		return ret;

	return vox_update_map(vox);
}

/**
 * vox_update_user_status() - update internally cached user enrolment states
 * @vox:	The main instance of struct clsic_vox used in this driver.
 * @start_phr:	Start biometric phrase ID.
 * @end_phr:	End biometric phrase ID.
 *
 * Find out which users are enrolled and cache this internally for all phrases
 * starting at start_phr and going up to and including end_phr.
 *
 * Return: errno.
 */
static int vox_update_user_status(struct clsic_vox *vox, uint8_t start_phr,
				  uint8_t end_phr)
{
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;
	int phr, usr;

	for (phr = start_phr; phr <= end_phr; phr++) {
		for (usr = CLSIC_VOX_USER1; usr <= CLSIC_VOX_USER3; usr++) {
			clsic_init_message(
				     (union t_clsic_generic_message *) &msg_cmd,
				     vox->service->service_instance,
				     CLSIC_VOX_MSG_CR_IS_USER_INSTALLED);
			msg_cmd.cmd_is_user_installed.userid = usr;
			msg_cmd.cmd_is_user_installed.phraseid = phr;

			ret = clsic_send_msg_sync_pm(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
			if (ret) {
				clsic_err(clsic,
					  "clsic_send_msg_sync %d.\n", ret);
				return -EIO;
			}

			switch (msg_rsp.rsp_is_user_installed.hdr.err) {
			case CLSIC_ERR_NONE:
				vox->user_installed[phr][usr] = true;
				break;
			case CLSIC_ERR_USER_NOT_INSTALLED:
				vox->user_installed[phr][usr] = false;
				break;
			default:
				vox->clsic_error_code =
					msg_rsp.rsp_is_user_installed.hdr.err;
				return -EIO;
			}
		}
	}

	return 0;
}

/**
 * vox_update_bio_pub_key() - update internally cached biometric public key for
 *			      this particular CLSIC device
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Query CLSIC to get its biometric public key and cache it internally.
 *
 * Return: errno.
 */
static int vox_update_bio_pub_key(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_GET_AUTH_KEY);

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  (uint8_t *) &vox->bio_pub_key,
				  sizeof(struct clsic_vox_auth_key));
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	/* Response is bulk in case of success. */
	if (clsic_get_bulk_bit(msg_rsp.blkrsp_get_auth_key.hdr.sbc))
		return 0;

	/*
	 * If it is not a bulk response then it is an fixed size error
	 * response.
	 */

	clsic_err(vox->clsic, "failed to get biometric public key: %d.\n",
		  msg_rsp.rsp_get_auth_key.hdr.err);
	return -EIO;
}

/**
 * vox_install_asset() - install an asset to CLSIC from the filesystem
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Based on the value of various ALSA controls, install an asset (either map,
 * VTE/SSF bin, or biometric phrase) to CLSIC.
 *
 * Return: errno.
 */
static int vox_install_asset(struct clsic_vox *vox)
{
	struct clsic *clsic = vox->clsic;
	const struct firmware *fw;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	char file[VOX_ASSET_TYPE_NAME_MAX_LEN];
	int id = -1;
	int ret;

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret) {
		set_error_info(vox, ret);
		goto exit;
	}

	/* vox->asset_type is auto-bounded by ALSA enum control. */
	snprintf(file, VOX_ASSET_TYPE_NAME_MAX_LEN,
		 vox_asset_filenames[vox->asset_type], vox->file_id);
	if (vox->asset_type == VOX_ASSET_TYPE_PHRASE)
		id = vox->phrase_id;
	else if (vox->asset_type != VOX_ASSET_TYPE_BIO_VTE_MAP)
		id = vox->bin_id;

	trace_clsic_vox_install_asset(file, id);

	ret = request_firmware(&fw, file, clsic->dev);
	if (ret) {
		clsic_err(clsic, "request_firmware failed for %s.\n", file);
		vox->error_info = VOX_ERROR_DRIVER;
		goto exit;
	}

	if (fw->size % CLSIC_ASSET_SIZE_ALIGNMENT) {
		clsic_err(clsic,
			  "firmware file %s size %d is not a multiple of %d.\n",
			  file, fw->size, CLSIC_ASSET_SIZE_ALIGNMENT);
		release_firmware(fw);
		vox->error_info = VOX_ERROR_DRIVER;
		goto exit;
	}

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_INSTALL_PHRASE);
		msg_cmd.cmd_install_phrase.hdr.bulk_sz = fw->size;
		msg_cmd.cmd_install_phrase.phraseid = vox->phrase_id;
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_INSTALL_BIN);
		msg_cmd.blkcmd_install_bin.hdr.bulk_sz = fw->size;
		msg_cmd.blkcmd_install_bin.binid = vox->bin_id;
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_INSTALL_BIOVTE_MAP);
		msg_cmd.blkcmd_install_biovte_map.hdr.bulk_sz = fw->size;
		break;
	}

	ret = clsic_send_msg_sync_pm(clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  fw->data, fw->size,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	release_firmware(fw);

	if (ret) {
		vox->error_info = VOX_ERROR_DRIVER;
		goto exit;
	}

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		if (msg_rsp.rsp_install_phrase.hdr.err == CLSIC_ERR_NONE) {
			vox->phrase_installed[vox->phrase_id] = true;
			clsic_dbg(clsic, "successfully installed phrase %d.\n",
				  vox->phrase_id);
			vox->error_info = VOX_ERROR_SUCCESS;

			/* Get updated information on enrolled users. */
			if (vox_update_user_status(vox, vox->phrase_id,
						   vox->phrase_id) != 0)
				vox->error_info = VOX_ERROR_DRIVER;
		} else {
			vox->error_info = VOX_ERROR_CLSIC;
			vox->clsic_error_code =
				msg_rsp.rsp_install_phrase.hdr.err;
		}
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		if (msg_rsp.rsp_install_bin.hdr.err == CLSIC_ERR_NONE) {
			vox->bin_installed[vox->bin_id] = true;
			clsic_dbg(clsic, "successfully installed bin %d.\n",
				  vox->bin_id);
			vox->error_info = VOX_ERROR_SUCCESS;
		} else {
			vox->error_info = VOX_ERROR_CLSIC;
			vox->clsic_error_code = msg_rsp.rsp_install_bin.hdr.err;
		}
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		if (msg_rsp.rsp_install_biovte_map.hdr.err == CLSIC_ERR_NONE) {
			vox->bio_vte_map_installed = true;
			clsic_dbg(clsic, "successfully installed bin %d.\n",
				  vox->bin_id);
			vox->error_info = VOX_ERROR_SUCCESS;
		} else {
			vox->error_info = VOX_ERROR_CLSIC;
			vox->clsic_error_code =
				msg_rsp.rsp_install_biovte_map.hdr.err;
		}
		break;
	}

exit:
	vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
	vox_send_userspace_event(vox);

	return ret;
}

/**
 * vox_uninstall_asset() - uninstall an asset from CLSIC
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Based on the value of various ALSA controls, uninstall an asset (either map,
 * VTE/SSF bin, or biometric phrase) from CLSIC.
 *
 * Return: errno.
 */
static int vox_uninstall_asset(struct clsic_vox *vox)
{
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret, usr;

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret) {
		set_error_info(vox, ret);
		goto exit;
	}

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		trace_clsic_vox_uninstall_phrase(vox->phrase_id);
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_REMOVE_PHRASE);
		msg_cmd.cmd_remove_phrase.phraseid = vox->phrase_id;
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		trace_clsic_vox_uninstall_bin(vox->bin_id);
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_REMOVE_BIN);
		msg_cmd.cmd_remove_bin.binid = vox->bin_id;
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		trace_clsic_vox_uninstall_bio_vte_map(0);
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_REMOVE_BIOVTE_MAP);
		break;
	}

	ret = clsic_send_msg_sync_pm(clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	if (ret) {
		clsic_err(clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_DRIVER;
		ret = -EIO;
		goto exit;
	}

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		switch (msg_rsp.rsp_remove_phrase.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_PHRASE_NOT_INSTALLED:
			clsic_dbg(clsic,
				  "successfully uninstalled phrase %d.\n",
				  vox->phrase_id);
			/*
			 * Present no enrolled users for this phrase to reflect
			 * what CLSIC reports when there is no phrase installed.
			 */
			for (usr = CLSIC_VOX_USER1;
			     usr <= CLSIC_VOX_USER3;
			     usr++)
				vox->user_installed[vox->phrase_id][usr] =
									false;
			vox->phrase_installed[vox->phrase_id] = false;
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		default:
			vox->clsic_error_code =
				msg_rsp.rsp_remove_phrase.hdr.err;
			vox->error_info = VOX_ERROR_CLSIC;
			ret = -EIO;
			break;
		}
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		switch (msg_rsp.rsp_remove_bin.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_BIN_NOT_INSTALLED:
			clsic_dbg(clsic, "successfully uninstalled bin %d.\n",
				  vox->bin_id);
			vox->bin_installed[vox->bin_id] = false;
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		default:
			vox->clsic_error_code = msg_rsp.rsp_remove_bin.hdr.err;
			vox->error_info = VOX_ERROR_CLSIC;
			ret = -EIO;
			break;
		}
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		switch (msg_rsp.rsp_remove_biovte_map.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_BIOVTE_MAP_NOT_INSTALLED:
			clsic_dbg(clsic,
				  "successfully uninstalled biometric VTE map %d.\n",
				  vox->bin_id);
			vox->bio_vte_map_installed = false;
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		default:
			vox->clsic_error_code =
				msg_rsp.rsp_remove_biovte_map.hdr.err;
			vox->error_info = VOX_ERROR_CLSIC;
			ret = -EIO;
			break;
		}
		break;
	}

exit:
	vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
	vox_send_userspace_event(vox);

	return ret;
}

/**
 * vox_remove_user() - remove an enrolled user from CLSIC
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Remove (or de-enrol) a particular user for a particular biometric phrase
 * based on the value of relevant ALSA controls set from userspace.
 *
 * Return: errno.
 */
static int vox_remove_user(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_remove_user(vox->user_id, vox->phrase_id);

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret) {
		set_error_info(vox, ret);
		goto exit;
	}

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_REMOVE_USER);
	msg_cmd.cmd_remove_user.phraseid = vox->phrase_id;
	msg_cmd.cmd_remove_user.userid = vox->user_id;

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_DRIVER;
		ret = -EIO;
		goto exit;
	}

	switch (msg_rsp.rsp_remove_user.hdr.err) {
	case CLSIC_ERR_NONE:
	case CLSIC_ERR_USER_NOT_INSTALLED:
		vox->user_installed[vox->phrase_id][vox->user_id] = false;
		vox->error_info = VOX_ERROR_SUCCESS;
		break;
	default:
		vox->clsic_error_code = msg_rsp.rsp_remove_user.hdr.err;
		vox->error_info = VOX_ERROR_CLSIC;
		ret = -EIO;
		break;
	}

exit:
	vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
	vox_send_userspace_event(vox);

	return ret;
}

/**
 * vox_start_enrol_user() - start the enrolment process on CLSIC
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Start enrolling a user by gathering information from various ALSA controls
 * then sending the appropriate message to CLSIC.
 *
 * Return: errno.
 */
static int vox_start_enrol_user(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_start_enrol_user(vox->user_id, vox->phrase_id,
					 vox->duration, vox->timeout,
					 vox->number_of_reps);

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_ENROL);
	if (ret) {
		set_error_info(vox, ret);
		goto exit;
	}

	vox_update_barge_in(vox);

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN);
	msg_cmd.cmd_install_user_begin.userid = vox->user_id;

	if ((vox->timeout > 0) && (vox->duration > 0)) {
		/* Implied combined enrolment.
		 *
		 *	phrase[0] must be a trigger phrase.
		 *	phrase[1] must be free speech (TI).
		 *	Number of reps must be same for both.
		 *	Trigger phrase will have a rep timeout.
		 *	Free speech will have a rep duration (6 second maximum).
		 */
		msg_cmd.cmd_install_user_begin.userid |=
						CLSIC_VOX_USER_FLAG_COMBINED;

		msg_cmd.cmd_install_user_begin.phrase[0].phraseid =
								vox->phrase_id;
		msg_cmd.cmd_install_user_begin.phrase[0].timeout_ms =
								vox->timeout;
		msg_cmd.cmd_install_user_begin.phrase[0].rep_count =
							vox->number_of_reps;

		msg_cmd.cmd_install_user_begin.phrase[1].phraseid =
							CLSIC_VOX_PHRASE_TI;
		msg_cmd.cmd_install_user_begin.phrase[1].duration_ms =
								vox->duration;
		msg_cmd.cmd_install_user_begin.phrase[1].rep_count =
							vox->number_of_reps;
	} else {
		if (vox->phrase_id == CLSIC_VOX_PHRASE_VDT1)
			msg_cmd.cmd_install_user_begin.phrase[0].timeout_ms =
								vox->timeout;
		else if (vox->phrase_id == CLSIC_VOX_PHRASE_TI)
			msg_cmd.cmd_install_user_begin.phrase[0].duration_ms =
								vox->duration;
		else {
			clsic_err(vox->clsic, "unsupported phrase ID %d.\n",
				  vox->phrase_id);
			vox->error_info = VOX_ERROR_DRIVER;
			ret = -EIO;
			goto exit;
		}

		msg_cmd.cmd_install_user_begin.phrase[0].phraseid =
								vox->phrase_id;
		msg_cmd.cmd_install_user_begin.phrase[0].rep_count =
							vox->number_of_reps;
	}

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_DRIVER;
		ret = -EIO;
		goto exit;
	}

	if (msg_rsp.rsp_install_user_begin.hdr.err == CLSIC_ERR_NONE)
		vox->error_info = VOX_ERROR_SUCCESS;
	else {
		vox->clsic_error_code = msg_rsp.rsp_install_user_begin.hdr.err;
		vox->error_info = VOX_ERROR_CLSIC;
		ret = -EIO;
	}

exit:
	if (ret)
		vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
	else
		vox_set_idle_and_state(vox, false, VOX_DRV_STATE_ENROLLING);

	vox_send_userspace_event(vox);

	return ret;
}

/**
 * vox_perform_enrol_rep() - perform an enrolment rep
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Tell CLSIC that we are about to perform an enrolment repetition.
 *
 * Return: errno.
 */
static int vox_perform_enrol_rep(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_perform_enrol_rep(0);

	/* Start the rep. */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_REP_START);

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_DRIVER;
		ret = -EIO;
	} else {
		switch (msg_rsp.rsp_rep_start.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_ONGOING_REP:
			break;
		default:
			vox->clsic_error_code = msg_rsp.rsp_rep_start.hdr.err;
			vox->error_info = VOX_ERROR_CLSIC;
			ret = -EIO;
		}
	}

	if (ret) {
		vox_set_idle_and_state(vox, false, VOX_DRV_STATE_ENROLLING);
		vox_send_userspace_event(vox);
	}

	return ret;
}

/**
 * vox_complete_enrolment() - complete an enrolment
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Tell CLSIC that we are ready to complete an enrolment, having started
 * enrolment and performed reps.
 *
 * Return: errno.
 */
static int vox_complete_enrolment(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_INSTALL_USER_COMPLETE);

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	trace_clsic_vox_complete_enrolment(0);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_DRIVER;
		ret = -EIO;
	} else if (msg_rsp.rsp_install_user_complete.hdr.err ==
		   CLSIC_ERR_NONE) {
		vox->user_installed[vox->phrase_id][vox->user_id] = true;
		if ((vox->timeout > 0) && (vox->duration > 0))
			/* Implied combined enrolment. */
			vox->user_installed[CLSIC_VOX_PHRASE_TI][vox->user_id] =
									true;
		vox->error_info = VOX_ERROR_SUCCESS;
	} else {
		vox->clsic_error_code =
			msg_rsp.rsp_install_user_complete.hdr.err;
		vox->error_info = VOX_ERROR_CLSIC;
		ret = -EIO;
	}

	vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
	vox_send_userspace_event(vox);

	return ret;
}

/**
 * vox_get_bio_results() - get biometric results from CLSIC
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Request biometric results from CLSIC. This function will be called once
 *
 * Return: errno.
 */
static int vox_get_bio_results(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_get_bio_results(0);

	memset(&vox->biometric_results, 0, sizeof(union bio_results_u));

	/*
	 * Firstly wait for CLSIC to notify us of new results. There are no
	 * further notifications after the last one if it contains the
	 * CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED error code.
	 */
	if (vox->auth_error != CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED)
		wait_for_completion(&vox->new_bio_results_completion);
	reinit_completion(&vox->new_bio_results_completion);

	switch (vox->auth_error) {
	case CLSIC_ERR_NONE:
		break;
	case CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED:
		/*
		 * For CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED, the maximum amount of
		 * audio has been processed however biometric results can still
		 * be obtained.
		 */
		vox->clsic_error_code = CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED;
		break;
	default:
		vox->clsic_error_code = vox->auth_error;
		vox->error_info = VOX_ERROR_CLSIC;
		ret = -EIO;
		goto exit;
	}

	/* Now get the results. */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_AUTH_USER);
	msg_cmd.blkcmd_auth_user.hdr.bulk_sz =
					sizeof(struct clsic_vox_auth_challenge);
	msg_cmd.blkcmd_auth_user.security_lvl = vox->security_level;
	msg_cmd.blkcmd_auth_user.result_format = vox->bio_results_format;

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  (uint8_t *) &vox->challenge,
				  sizeof(struct clsic_vox_auth_challenge),
				  (uint8_t *) &vox->biometric_results,
				  size_of_bio_results(vox->bio_results_format));
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_DRIVER;
		ret = -EIO;
		goto exit;
	}

	/* Response is either bulk in case of success or fixed on failure. */
	if (clsic_get_bulk_bit(msg_rsp.rsp_auth_user.hdr.sbc)) {
		vox->error_info = VOX_ERROR_SUCCESS;
#ifdef CONFIG_DEBUG_FS
		/* Save the auth data for later analysis */
		memcpy(&vox->last_auth.msg, &msg_rsp,
		       sizeof(union clsic_vox_msg));
		memcpy(&vox->last_auth.result, &vox->biometric_results,
		       size_of_bio_results(vox->bio_results_format));
		vox->last_auth.result_format = vox->bio_results_format;
		vox->last_auth.security_lvl = vox->security_level;
		vox->last_auth.blob.size = sizeof(vox->last_auth.msg) +
			size_of_bio_results(vox->bio_results_format);
#endif
	} else {
		vox->clsic_error_code = msg_rsp.rsp_auth_user.hdr.err;
		vox->error_info = VOX_ERROR_CLSIC;
		ret = -EIO;
	}

exit:
	mutex_lock(&vox->drv_state_lock);
	if (vox->drv_state == VOX_DRV_STATE_GETTING_BIO_RESULTS)
		vox_set_idle_and_state(vox, false, VOX_DRV_STATE_STREAMING);
	mutex_unlock(&vox->drv_state_lock);

	vox_send_userspace_event(vox);

	return ret;
}

/**
 * vox_stop_bio_results() - no longer get biometric results from CLSIC
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * Tell CLSIC that we will no longer be requesting any biometric results by
 * switching CLSIC to IDLE mode in preparation for the next operation. Obviously
 * it will not be possible to get biometric results any more after calling this.
 *
 * Return: errno.
 */
static void vox_stop_bio_results(struct clsic_vox *vox)
{
	trace_clsic_vox_stop_bio_results(0);

	mutex_lock(&vox->drv_state_lock);

	if (vox->drv_state == VOX_DRV_STATE_STOPPING_BIO_RESULTS)
		vox->drv_state = VOX_DRV_STATE_STREAMING;

	vox->error_info = VOX_ERROR_SUCCESS;
	vox_send_userspace_event(vox);

	mutex_unlock(&vox->drv_state_lock);
}

/**
 * vox_drv_state_handler() - handle userspace commands from the driver state
 *				control
 * @data:	Used to obtain the main instance of struct clsic_vox used in
 *		this driver in which this is contained.
 *
 * Work function allows ALSA "get" control to return immediately while sending
 * multiple messages.
 *
 * Return: errno.
 */
static void vox_drv_state_handler(struct work_struct *data)
{
	struct clsic_vox *vox = container_of(data, struct clsic_vox,
					     drv_state_work);
	struct clsic *clsic = vox->clsic;
	int ret;

	switch (vox->drv_state) {
	case VOX_DRV_STATE_INSTALLING_ASSET:
		ret = vox_install_asset(vox);
		if (ret)
			clsic_err(clsic, "vox_install_asset ret %d.\n", ret);
		break;
	case VOX_DRV_STATE_UNINSTALLING_ASSET:
		ret = vox_uninstall_asset(vox);
		if (ret)
			clsic_err(clsic, "vox_uninstall_asset ret %d.\n", ret);
		break;
	case VOX_DRV_STATE_REMOVING_USER:
		ret = vox_remove_user(vox);
		if (ret)
			clsic_err(clsic, "vox_remove_user ret %d.\n", ret);
		break;
	case VOX_DRV_STATE_STARTING_ENROL:
		ret = vox_start_enrol_user(vox);
		if (ret)
			clsic_err(clsic, "vox_start_enrol_user ret %d.\n", ret);
		break;
	case VOX_DRV_STATE_PERFORMING_ENROL_REP:
		ret = vox_perform_enrol_rep(vox);
		if (ret)
			clsic_err(clsic, "vox_perform_enrol_rep ret %d.\n",
				  ret);
		break;
	case VOX_DRV_STATE_COMPLETING_ENROL:
		ret = vox_complete_enrolment(vox);
		if (ret)
			clsic_err(clsic, "vox_complete_enrolment ret %d.\n",
				  ret);
		break;
	case VOX_DRV_STATE_TERMINATING_ENROL:
		vox->error_info = VOX_ERROR_SUCCESS;
		vox_set_idle_and_state(vox, true, VOX_DRV_STATE_NEUTRAL);
		vox_send_userspace_event(vox);
		break;
	case VOX_DRV_STATE_GETTING_BIO_RESULTS:
		ret = vox_get_bio_results(vox);
		if (ret)
			clsic_err(clsic, "vox_get_bio_results ret %d.\n", ret);
		break;
	case VOX_DRV_STATE_STOPPING_BIO_RESULTS:
		vox_stop_bio_results(vox);
		break;
	default:
		clsic_err(clsic, "unknown state %d for scheduled work.\n",
			  vox->drv_state);
	}
}

/**
 * vox_ctrl_error_info_put() - clear the error info control
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow the user to clear the error info ALSA control by writing the
 * appropriate value to it.
 *
 * Return: errno.
 */
static int vox_ctrl_error_info_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_error_info);

	if (ucontrol->value.enumerated.item[0] != VOX_ERROR_CLEARED)
		return -EINVAL;

	vox->error_info = VOX_ERROR_CLEARED;
	vox->clsic_error_code = CLSIC_ERR_NONE;

	return 0;
}

/**
 * vox_ctrl_int_get() - handle getting of all INT kcontrols
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * By using dobj->private_value as set at control creation time, we can use this
 * generic function to allow userspace to get the relevant internal variable
 * existing in the driver vox struct.
 *
 * Return: 0 always.
 */
static int vox_ctrl_int_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *e =
			(struct soc_mixer_control *) kcontrol->private_value;

	ucontrol->value.enumerated.item[0] = *(unsigned int *) e->dobj.private;

	return 0;
}

/**
 * vox_ctrl_int_put() - handle putting of all INT kcontrols
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * By using dobj->private_value as set at control creation time, we can use this
 * generic function to allow userspace to set the relevant internal variable
 * existing in the driver vox struct.
 *
 * Return: 0 always.
 */
static int vox_ctrl_int_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *e =
			(struct soc_mixer_control *) kcontrol->private_value;

	*(unsigned int *) e->dobj.private = ucontrol->value.enumerated.item[0];

	return 0;
}

/**
 * vox_ctrl_enum_get() - handle getting of all ENUM kcontrols
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * By using dobj->private_value as set at control creation time, we can use this
 * generic function to allow userspace to get the relevant internal variable
 * existing in the driver vox struct.
 *
 * Return: 0 always.
 */
static int vox_ctrl_enum_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;

	ucontrol->value.enumerated.item[0] = *(unsigned int *) e->dobj.private;

	return 0;
}

/**
 * vox_ctrl_enum_put() - handle putting of all ENUM kcontrols
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * By using dobj->private_value as set at control creation time, we can use this
 * generic function to allow userspace to set the relevant internal variable
 * existing in the driver vox struct.
 *
 * Return: 0 always.
 */
static int vox_ctrl_enum_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;

	*(unsigned int *) e->dobj.private = ucontrol->value.enumerated.item[0];

	return 0;
}

/**
 * vox_ctrl_challenge_get() - read the challenge bytes for biometric
 *			      authentication
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow userspace to get the bytes used as a cryptographic challenge to CLSIC
 * when getting biometric authentication results.
 *
 * Return: 0 always.
 */
static int vox_ctrl_challenge_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *s_bytes_challenge =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(s_bytes_challenge, struct clsic_vox,
			     s_bytes_challenge);

	memcpy(ucontrol->value.bytes.data, &vox->challenge,
	       s_bytes_challenge->max);

	return 0;
}

/**
 * vox_ctrl_challenge_put() - write the challenge bytes for biometric
 *			      authentication
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow userspace to set the bytes used as a cryptographic challenge to CLSIC
 * when getting biometric authentication results.
 *
 * Return: 0 always.
 */
static int vox_ctrl_challenge_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *s_bytes_challenge =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(s_bytes_challenge, struct clsic_vox,
			     s_bytes_challenge);

	memcpy(&vox->challenge, ucontrol->value.bytes.data,
	       s_bytes_challenge->max);

	return 0;
}

/**
 * vox_ctrl_bio_res_blob() - move signed biometrics authentication results data
 *			blob to userspace
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @op_flag:	Can only usefully be SNDRV_CTL_TLV_OP_READ.
 * @size:	Unused.
 * @tlv:	ALSA TLV data, effectively a byte buffer.
 *
 * Allow userspace to get the signed blob that contains the biometric
 * authentication results that identify users and other information.
 *
 * Return: errno.
 */
static int vox_ctrl_bio_res_blob(struct snd_kcontrol *kcontrol,
				 int op_flag,
				 unsigned int size,
				 unsigned int __user *tlv)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(be, struct clsic_vox, s_bytes_bio_res);

	if (op_flag == SNDRV_CTL_TLV_OP_WRITE)
		return -EACCES;
	if (copy_to_user(tlv, &vox->biometric_results,
			 size_of_bio_results(vox->bio_results_format)))
		return -EFAULT;

	return 0;
}

/**
 * vox_ctrl_bio_pub_key_get() - obtain the public signing key of CLSIC
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow userspace to get CLSIC's public key as used within the biometric
 * results blob.
 *
 * Return: 0 always.
 */
static int vox_ctrl_bio_pub_key_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *s_bytes_bio_pub_key =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(s_bytes_bio_pub_key, struct clsic_vox,
			     s_bytes_bio_pub_key);

	memcpy(ucontrol->value.bytes.data, &vox->bio_pub_key,
	       s_bytes_bio_pub_key->max);

	return 0;
}

/**
 * vox_ctrl_asset_installed_get() - find out if a particular asset is installed
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Depending on the asset type, return to userspace whether an asset is
 * installed or not.
 *
 * Return: errno.
 */
static int vox_ctrl_asset_installed_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct clsic_vox *vox = (struct clsic_vox *) kcontrol->private_value;

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		ucontrol->value.integer.value[0] =
					vox->phrase_installed[vox->phrase_id];
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		ucontrol->value.integer.value[0] =
					vox->bin_installed[vox->bin_id];
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		ucontrol->value.integer.value[0] =
					vox->bio_vte_map_installed;
		break;
	}

	return 0;
}

/**
 * vox_ctrl_dummy() - dummy function for read/writing of INT controls
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure (unused).
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure
 *		(unused).
 *
 * The ALSA sanity tests require that all controls are readable and writeable,
 * so this dummy function performs the purpose of filling in when a control has
 * no purpose to be either read or written.
 *
 * Return: 0 always.
 */
static int vox_ctrl_dummy(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

/**
 * vox_ctrl_user_installed_get() - get user enrolment statuses
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure (unused).
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow userspace to see whether a user has been enrolled for a particular
 * phrase or not. The user and phrase ID controls will also need to be set
 * appropriately before reading this control.
 *
 * Return: 0 always.
 */
static int vox_ctrl_user_installed_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct clsic_vox *vox = (struct clsic_vox *) kcontrol->private_value;

	ucontrol->value.integer.value[0] =
			vox->user_installed[vox->phrase_id][vox->user_id];

	return 0;
}

/**
 * vox_update_barge_in() - tell CLSIC about enrolment state
 * @vox:	The main instance of struct clsic_vox used in this driver.
 *
 * This needs to be called just for safety when enrolment starts or when a
 * trigger has occurred. Alternatively, it may be actively called during
 * enrolment reps or biometric authentication.
 *
 * Return: errno.
 */
static int vox_update_barge_in(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int msgid;
	int ret;

	if (vox->barge_in_status == VOX_BARGE_IN_ENABLED)
		msgid = CLSIC_VOX_MSG_CR_BARGE_IN_ENA;
	else
		msgid = CLSIC_VOX_MSG_CR_BARGE_IN_DIS;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance, msgid);

	ret = clsic_send_msg_sync_pm(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	/* rsp_barge_in_ena and rsp_barge_in_dis are identical. */
	if (msg_rsp.rsp_barge_in_ena.hdr.err != CLSIC_ERR_NONE) {
		clsic_err(vox->clsic, "unexpected CLSIC error code %d.\n",
			  msg_rsp.rsp_barge_in_ena.hdr.err);
		return -EIO;
	}

	return 0;
}

/**
 * vox_ctrl_barge_in_put() - ALSA control put function for barge-in
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure (unused).
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Set the barge-in status on CLSIC, but only if it will have an immediate
 * effect.
 *
 * Return: errno.
 */
static int vox_ctrl_barge_in_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_barge_in);

	vox->barge_in_status = ucontrol->value.enumerated.item[0];

	/* Only set barge-in now if CLSIC is already doing something. */
	if (vox->drv_state != VOX_DRV_STATE_NEUTRAL)
		return vox_update_barge_in(vox);

	return 0;
}

/**
 * vox_ctrl_drv_state_put() - userspace control tells CLSIC to perform a
 *			      particular action
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure (unused).
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Userspace can get the driver to perform particular actions by writing to the
 * driver state ALSA control. The control then changes enumeration so that a
 * read of the control from userspace provides information about the current
 * action being undertaken.
 *
 * Return: errno.
 */
static int vox_ctrl_drv_state_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_mode);
	int ret = 0;

	if (ucontrol->value.enumerated.item[0] == vox->drv_state)
		return 0;

	mutex_lock(&vox->drv_state_lock);

	switch (ucontrol->value.enumerated.item[0]) {
	case VOX_DRV_STATE_GET_BIO_RESULTS:
		if (vox->drv_state == VOX_DRV_STATE_STREAMING) {
			vox->drv_state = VOX_DRV_STATE_GETTING_BIO_RESULTS;
			mutex_unlock(&vox->drv_state_lock);
		} else {
			mutex_unlock(&vox->drv_state_lock);
			ret = -EBUSY;
		}
		break;
	case VOX_DRV_STATE_STOP_BIO_RESULTS:
		/*
		 * TODO: remove stop biometrics entirely as it is now managed
		 * by simply closing the compressed audio path.
		 */
		trace_clsic_vox_stop_bio_results(0);

		mutex_unlock(&vox->drv_state_lock);
		vox->error_info = VOX_ERROR_SUCCESS;
		vox_send_userspace_event(vox);

		return 0;
	case VOX_DRV_STATE_INSTALL_ASSET:
	case VOX_DRV_STATE_UNINSTALL_ASSET:
	case VOX_DRV_STATE_REMOVE_USER:
	case VOX_DRV_STATE_START_ENROL:
		if (vox->drv_state == VOX_DRV_STATE_NEUTRAL) {
			/*
			 * Management mode goes from command
			 * e.g. INSTALL to a state e.g. INSTALLING
			 */
			vox->drv_state =
				ucontrol->value.enumerated.item[0] + 1;
			mutex_unlock(&vox->drv_state_lock);
		} else {
			mutex_unlock(&vox->drv_state_lock);
			ret = -EBUSY;
		}
		break;
	case VOX_DRV_STATE_PERFORM_ENROL_REP:
	case VOX_DRV_STATE_COMPLETE_ENROL:
	case VOX_DRV_STATE_TERMINATE_ENROL:
		if (vox->drv_state == VOX_DRV_STATE_ENROLLING) {
			vox->drv_state =
				ucontrol->value.enumerated.item[0] + 1;
			mutex_unlock(&vox->drv_state_lock);
		} else {
			mutex_unlock(&vox->drv_state_lock);
			ret = -EBUSY;
		}
		break;
	default:
		mutex_unlock(&vox->drv_state_lock);
		ret = -EINVAL;
	}

	if (ret == 0) {
		if (schedule_work(&vox->drv_state_work) == false) {
			clsic_info(vox->clsic,
				   "flush scheduled work and reschedule: state %d\n",
				   vox->drv_state);
			flush_scheduled_work();
			schedule_work(&vox->drv_state_work);
		}
	} else
		clsic_err(vox->codec,
			  "unable to switch from vox driver state %d to %d (error %d).\n",
			  vox->drv_state,
			  ucontrol->value.enumerated.item[0],
			  ret);

	return ret;
}

/**
 * vox_notification_handler() - handle notifications destined for the vox
 *				service
 * @clsic:	The main shared instance of struct clsic used in the CLSIC
 *		drivers.
 * @handler:	The handler struct for vox service.
 * @msg:	The message notification itself as received from CLSIC.
 *
 * This is a standard CLSIC function that will be called in the interrupt
 * handler context in the core messaging driver to examine notifications for the
 * vox service and react accordingly.
 *
 * Return: CLSIC_HANDLED or CLSIC_UNHANDLED.
 */
static int vox_notification_handler(struct clsic *clsic,
				    struct clsic_service *handler,
				    struct clsic_message *msg)
{
	struct clsic_vox *vox = (struct clsic_vox *) handler->data;
	enum clsic_vox_msg_id msgid;
	union clsic_vox_msg *msg_nty = (union clsic_vox_msg *) &msg->fsm;
	int ret = CLSIC_HANDLED;

	msgid = clsic_get_messageid(msg);

	switch (msgid) {
	case CLSIC_VOX_MSG_N_LISTEN_ERR:
		/*
		 * There was an error while the voice service was listening for
		 * a trigger.
		 */
		trace_clsic_vox_trigger_heard(false);

		clsic_err(clsic, "trigger detection error on CLSIC %d.\n",
			  msg_nty->nty_listen_err.err);

		vox->asr_stream.listen_error = true;
		complete(&vox->asr_stream.completion);

		break;
	case CLSIC_VOX_MSG_N_TRGR_DETECT:
		/*
		 * On trigger CLSIC has transitioned from LISTEN to STREAM by
		 * itself.
		 */
		trace_clsic_vox_trigger_heard(true);
		vox->clsic_mode = CLSIC_VOX_MODE_STREAM;

		/*
		 * Prevent the messaging processor from being powered off while
		 * streaming
		 */
		clsic_msgproc_use(clsic, vox->service->service_instance);

		complete(&vox->asr_stream.completion);

		break;
	case CLSIC_VOX_MSG_N_REP_COMPLETE:
		if (msg_nty->nty_rep_complete.err == CLSIC_ERR_NONE)
			vox->error_info = VOX_ERROR_SUCCESS;
		else {
			vox->clsic_error_code = msg_nty->nty_rep_complete.err;
			vox->error_info = VOX_ERROR_CLSIC;
		}

		vox_set_idle_and_state(vox, false, VOX_DRV_STATE_ENROLLING);
		vox_send_userspace_event(vox);

		break;
	case CLSIC_VOX_MSG_N_NEW_AUTH_RESULT:
		/*
		 * It is expected that the host tracks these messages and when
		 * the desired number of audio frames are processed by the
		 * CLSIC, the host gets the results from the CLSIC.
		 */
		trace_clsic_vox_new_auth_result(msg_nty);

		vox->auth_error = msg_nty->nty_new_auth_result.auth_stop_reason;

		complete(&vox->new_bio_results_completion);

		break;
	default:
		clsic_err(clsic, "unrecognised message with message ID %d\n",
			  msgid);
		ret = CLSIC_UNHANDLED;
	}

	return ret;
}

/**
 * vox_ctrl_int_info() - callback to provide information of vox integer controls
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @uinfo:	struct snd_ctl_elem_info as used by the ALSA infrastructure.
 *
 * Allow userspace to write virtual SCC control registers.
 *
 * Return: 0 always.
 */
static int vox_ctrl_int_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct soc_mreg_control *mc =
		(struct soc_mreg_control *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = mc->min;
	uinfo->value.integer.max = mc->max;

	return 0;
}

/**
 * vox_ctrl_int_helper() - set up an integer ALSA control
 * @kc:			struct snd_kcontrol_new as used by the ALSA
 *			infrastructure.
 * @control_name:	Name of this ALSA control.
 * @private_value:	struct soc_mixer_control to embed in the control, as
 *			used by the put and get functions.
 *
 * Helper function to speed up creation of an ALSA control.
 *
 */
static void vox_ctrl_int_helper(struct snd_kcontrol_new *kc,
				const char *control_name,
				unsigned long private_value)
{
	kc->name = control_name;
	kc->info = vox_ctrl_int_info;
	kc->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kc->get = vox_ctrl_int_get;
	kc->put = vox_ctrl_int_put;
	kc->private_value = private_value;
	kc->access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		     SNDRV_CTL_ELEM_ACCESS_VOLATILE;
}

/**
 * vox_ctrl_enum_helper() - set up an enum ALSA control
 * @kc:			struct snd_kcontrol_new as used by the ALSA
 *			infrastructure.
 * @control_name:	Name of this ALSA control.
 * @private_value:	struct soc_mixer_control to embed in the control, as
 *			used by the put and get functions.
 *
 * Helper function to speed up creation of an ALSA control.
 *
 */
static void vox_ctrl_enum_helper(struct snd_kcontrol_new *kc,
				 const char *control_name,
				 unsigned long private_value)
{
	kc->name = control_name;
	kc->info = snd_soc_info_enum_double;
	kc->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kc->get = vox_ctrl_enum_get;
	kc->put = vox_ctrl_enum_put;
	kc->private_value = private_value;
	kc->access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		     SNDRV_CTL_ELEM_ACCESS_VOLATILE;
}

/**
 * vox_ctrl_scc_get() - read the bytes for commonly used SCC controls
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow userspace to read virtual SCC control registers.
 *
 * Return: errno.
 */
static int vox_ctrl_scc_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *s_bytes_scc =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox = (struct clsic_vox *) s_bytes_scc->dobj.private;
	uint32_t rgstr;
	__be32 rgstr_be;

	if (s_bytes_scc == &vox->s_bytes_scc_manage_ack)
		/*
		 * As we are simulating the SCCManageAckCtrl register on
		 * a codec, we ensure that reads always get 0.
		 */
		rgstr = 0;
	else if (s_bytes_scc == &vox->s_bytes_scc_status)
		rgstr = vox->scc_status;
	else if (s_bytes_scc == &vox->s_bytes_scc_cap_delay_ms)
		rgstr = vox->scc_cap_delay_ms;
	else if (s_bytes_scc == &vox->s_bytes_scc_triggerpoint)
		rgstr = vox->scc_triggerpoint;
	else if (s_bytes_scc == &vox->s_bytes_scc_cap_preamble_ms)
		rgstr = vox->scc_cap_preamble_ms;
	else if (s_bytes_scc == &vox->s_bytes_scc_phraseid)
		rgstr = vox->trigger_phrase_id;
	else {
		clsic_err(vox->clsic, "unrecognised accessor %p\n",
			  s_bytes_scc);
		return -EINVAL;
	}

	rgstr_be = cpu_to_be32(rgstr);

	if (!memcpy(ucontrol->value.bytes.data, &rgstr_be, sizeof(uint32_t)))
		return -EIO;

	return 0;
}

/**
 * vox_ctrl_scc_put() - write the bytes for commonly used SCC controls
 * @kcontrol:	struct snd_kcontrol as used by the ALSA infrastructure.
 * @ucontrol:	struct snd_ctl_elem_value as used by the ALSA infrastructure.
 *
 * Allow userspace to write virtual SCC control registers.
 *
 * Return: errno.
 */
static int vox_ctrl_scc_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *s_bytes_scc =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox = (struct clsic_vox *) s_bytes_scc->dobj.private;
	__be32 rgstr;

	if (s_bytes_scc == &vox->s_bytes_scc_manage_ack) {
		if (!memcpy(&rgstr, ucontrol->value.bytes.data,
			    sizeof(uint32_t)))
			return -EIO;

		if ((vox->drv_state == VOX_DRV_STATE_STREAMING) ||
		    (vox->drv_state == VOX_DRV_STATE_GETTING_BIO_RESULTS)) {
			if (be32_to_cpu(rgstr) & CTRL_ACK_VTE1_TRIG)
				vox->scc_status &=
						(~VTE1_TRIGGERED_MOST_RECENT);
			if (be32_to_cpu(rgstr) & CTRL_ACK_STOP_STREAM)
				vox->scc_status = 0;
		}
	}

	return 0;
}

/**
 * vox_ctrl_scc_helper() - set up an ALSA control for use by the SCC userspace
 *			   infrastructure
 * @kcontrol:		struct snd_kcontrol as used by the ALSA infrastructure.
 * @control_name:	Name of this ALSA control.
 * @private_value:	struct soc_mixer_control to embed in the control, as
 *			used by the put and get functions.
 *
 * Helper function to speed up creation of an ALSA control for use by the
 * userspace SCC libraries - these are specified to have a particular size and
 * type.
 *
 */
static void vox_ctrl_scc_helper(struct snd_kcontrol_new *kc,
				const char *control_name,
				struct soc_bytes_ext *s_bytes_var,
				struct clsic_vox *vox)
{
	s_bytes_var->max = sizeof(uint32_t);
	kc->name = control_name;
	kc->info = snd_soc_bytes_info_ext;
	kc->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kc->get = vox_ctrl_scc_get;
	kc->put = vox_ctrl_scc_put;
	kc->private_value = (unsigned long) s_bytes_var;
	kc->access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		     SNDRV_CTL_ELEM_ACCESS_VOLATILE;
	s_bytes_var->dobj.private = vox;
}

/**
 * clsic_vox_codec_probe() - probe function for the codec part of the driver
 * @codec:	The main shared instance of struct snd_soc_codec used in
 *		CLSIC.
 *
 * Create ALSA controls and call various update functions to cache information
 * in the driver from CLSIC.
 *
 * Return: errno.
 */
static int clsic_vox_codec_probe(struct snd_soc_codec *codec)
{
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(codec);
	struct clsic_service *handler = vox->service;
	int ret;
	unsigned int ctl_id = 0;

	dev_info(codec->dev, "%s() %p.\n", __func__, codec);

	vox->codec = codec;
	vox->drv_state = VOX_DRV_STATE_NEUTRAL;
	vox->clsic_mode = CLSIC_VOX_MODE_IDLE;

	mutex_init(&vox->drv_state_lock);

	INIT_WORK(&vox->drv_state_work, vox_drv_state_handler);

	init_completion(&vox->asr_stream.completion);
	mutex_init(&vox->asr_stream.stream_lock);

	vox->soc_enum_mode.items = VOX_NUM_DRV_STATES;
	vox->soc_enum_mode.texts = vox_drv_state_text;
	vox->soc_enum_mode.dobj.private = &vox->drv_state;
	vox_ctrl_enum_helper(&vox->kcontrol_new[ctl_id],
			     "Vox Driver State",
			     (unsigned long) &vox->soc_enum_mode);
	vox->kcontrol_new[ctl_id].put = vox_ctrl_drv_state_put;

	ctl_id++;
	vox->error_info = VOX_ERROR_CLEARED;

	vox->soc_enum_error_info.items = VOX_NUM_ERRORS;
	vox->soc_enum_error_info.texts = vox_error_info_text;
	vox->soc_enum_error_info.dobj.private = &vox->error_info;
	vox_ctrl_enum_helper(&vox->kcontrol_new[ctl_id],
			     "Vox Error Info",
			     (unsigned long) &vox->soc_enum_error_info);
	vox->kcontrol_new[ctl_id].put = vox_ctrl_error_info_put;

	ctl_id++;
	vox->clsic_error_code = CLSIC_ERR_NONE;

	memset(&vox->clsic_error_mixer_ctrl, 0,
	       sizeof(vox->clsic_error_mixer_ctrl));
	vox->clsic_error_mixer_ctrl.min = CLSIC_ERR_NONE;
	vox->clsic_error_mixer_ctrl.max = CLSIC_ERR_BL_OSAPP_NOT_2ND_IN_CAB;
	vox->clsic_error_mixer_ctrl.platform_max =
					CLSIC_ERR_BL_OSAPP_NOT_2ND_IN_CAB;
	vox->clsic_error_mixer_ctrl.dobj.private = &vox->clsic_error_code;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox CLSIC Error Code",
			    (unsigned long) &vox->clsic_error_mixer_ctrl);
	vox->kcontrol_new[ctl_id].put = vox_ctrl_dummy;

	ctl_id++;
	vox->phrase_id = CLSIC_VOX_PHRASE_VDT1;

	memset(&vox->phrase_id_mixer_ctrl, 0,
	       sizeof(vox->phrase_id_mixer_ctrl));
	vox->phrase_id_mixer_ctrl.min = 0;
	vox->phrase_id_mixer_ctrl.max = VOX_MAX_PHRASES - 1;
	vox->phrase_id_mixer_ctrl.platform_max = VOX_MAX_PHRASES - 1;
	vox->phrase_id_mixer_ctrl.dobj.private = &vox->phrase_id;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Phrase ID",
			    (unsigned long) &vox->phrase_id_mixer_ctrl);

	ctl_id++;
	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret != 0)
		return ret;

	ret = vox_update_assets_status(vox);
	if (ret != 0)
		return ret;

	vox->kcontrol_new[ctl_id].name = "Vox Asset Installed";
	vox->kcontrol_new[ctl_id].info = snd_soc_info_bool_ext;
	vox->kcontrol_new[ctl_id].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[ctl_id].get = vox_ctrl_asset_installed_get;
	vox->kcontrol_new[ctl_id].put = vox_ctrl_dummy;
	vox->kcontrol_new[ctl_id].private_value = (unsigned long)vox;
	vox->kcontrol_new[ctl_id].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					   SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ctl_id++;
	vox->user_id = CLSIC_VOX_USER1;

	memset(&vox->user_id_mixer_ctrl, 0, sizeof(vox->user_id_mixer_ctrl));
	vox->user_id_mixer_ctrl.min = CLSIC_VOX_USER1;
	vox->user_id_mixer_ctrl.max = CLSIC_VOX_USER3;
	vox->user_id_mixer_ctrl.platform_max = CLSIC_VOX_USER3;
	vox->user_id_mixer_ctrl.dobj.private = &vox->user_id;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox User ID",
			    (unsigned long) &vox->user_id_mixer_ctrl);

	ctl_id++;
	ret = vox_update_user_status(vox, CLSIC_VOX_PHRASE_VDT1,
				     CLSIC_VOX_PHRASE_TI);
	if (ret != 0)
		return ret;

	vox->kcontrol_new[ctl_id].name = "Vox User Installed";
	vox->kcontrol_new[ctl_id].info = snd_soc_info_bool_ext;
	vox->kcontrol_new[ctl_id].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[ctl_id].get = vox_ctrl_user_installed_get;
	vox->kcontrol_new[ctl_id].put = vox_ctrl_dummy;
	vox->kcontrol_new[ctl_id].private_value = (unsigned long)vox;
	vox->kcontrol_new[ctl_id].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					   SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ctl_id++;
	vox->duration = VOX_DEFAULT_DURATION;

	memset(&vox->duration_mixer_ctrl, 0, sizeof(vox->duration_mixer_ctrl));
	vox->duration_mixer_ctrl.min = 0;
	vox->duration_mixer_ctrl.max = VOX_MAX_DURATION_TIMEOUT;
	vox->duration_mixer_ctrl.platform_max = VOX_MAX_DURATION_TIMEOUT;
	vox->duration_mixer_ctrl.dobj.private = &vox->duration;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Duration in ms",
			    (unsigned long) &vox->duration_mixer_ctrl);

	ctl_id++;
	vox->timeout = VOX_DEFAULT_TIMEOUT;

	memset(&vox->timeout_mixer_ctrl, 0, sizeof(vox->timeout_mixer_ctrl));
	vox->timeout_mixer_ctrl.min = 0;
	vox->timeout_mixer_ctrl.max = VOX_MAX_DURATION_TIMEOUT;
	vox->timeout_mixer_ctrl.platform_max = VOX_MAX_DURATION_TIMEOUT;
	vox->timeout_mixer_ctrl.dobj.private = &vox->timeout;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Timeout in ms",
			    (unsigned long) &vox->timeout_mixer_ctrl);

	ctl_id++;
	vox->number_of_reps = VOX_DEFAULT_NUM_REPS;

	memset(&vox->reps_mixer_ctrl, 0, sizeof(vox->reps_mixer_ctrl));
	vox->reps_mixer_ctrl.min = 1;
	vox->reps_mixer_ctrl.max = VOX_MAX_NUM_REPS;
	vox->reps_mixer_ctrl.platform_max = VOX_MAX_NUM_REPS;
	vox->reps_mixer_ctrl.dobj.private = &vox->number_of_reps;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Number of Enrolment Repetitions",
			    (unsigned long) &vox->reps_mixer_ctrl);

	ctl_id++;
	vox->security_level = VOX_SEC_LEVEL_LOW;

	vox->soc_enum_sec_level.items = VOX_NUM_SEC_LEVEL;
	vox->soc_enum_sec_level.texts = vox_sec_level_text;
	vox->soc_enum_sec_level.dobj.private = &vox->security_level;
	vox_ctrl_enum_helper(&vox->kcontrol_new[ctl_id],
			     "Vox Security Level",
			     (unsigned long) &vox->soc_enum_sec_level);

	ctl_id++;
	vox->bio_results_format = VOX_BIO_RESULTS_CLASSIC;

	vox->soc_enum_bio_res_type.items = VOX_NUM_BIO_RESULTS_FORMATS;
	vox->soc_enum_bio_res_type.texts = vox_bio_results_format_text;
	vox->soc_enum_bio_res_type.dobj.private = &vox->bio_results_format;
	vox_ctrl_enum_helper(&vox->kcontrol_new[ctl_id],
			     "Vox Biometric Results Format",
			     (unsigned long) &vox->soc_enum_bio_res_type);

	ctl_id++;
	memset(&vox->challenge, 0, sizeof(struct clsic_vox_auth_challenge));

	vox->s_bytes_challenge.max = sizeof(struct clsic_vox_auth_challenge);
	vox->kcontrol_new[ctl_id].name = "Vox Challenge";
	vox->kcontrol_new[ctl_id].info = snd_soc_bytes_info_ext;
	vox->kcontrol_new[ctl_id].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[ctl_id].get = vox_ctrl_challenge_get;
	vox->kcontrol_new[ctl_id].put = vox_ctrl_challenge_put;
	vox->kcontrol_new[ctl_id].private_value =
				(unsigned long) &vox->s_bytes_challenge;
	vox->kcontrol_new[ctl_id].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					   SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ctl_id++;
	memset(&vox->biometric_results, 0, sizeof(union bio_results_u));

	vox->s_bytes_bio_res.max = sizeof(union bio_results_u);
	vox->kcontrol_new[ctl_id].name = "Vox Signed Biometric Results Blob";
	vox->kcontrol_new[ctl_id].info = snd_soc_bytes_info_ext;
	vox->kcontrol_new[ctl_id].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[ctl_id].tlv.c = vox_ctrl_bio_res_blob;
	vox->kcontrol_new[ctl_id].private_value =
				(unsigned long) &vox->s_bytes_bio_res;
	vox->kcontrol_new[ctl_id].access = SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE |
					   SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
					   SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ctl_id++;
	memset(&vox->bio_pub_key, 0, sizeof(struct clsic_vox_auth_key));
	ret = vox_update_bio_pub_key(vox);
	if (ret != 0)
		return ret;

	vox->s_bytes_bio_pub_key.max = sizeof(struct clsic_vox_auth_key);
	vox->kcontrol_new[ctl_id].name = "Vox Biometric Result Public Key";
	vox->kcontrol_new[ctl_id].info = snd_soc_bytes_info_ext;
	vox->kcontrol_new[ctl_id].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[ctl_id].get = vox_ctrl_bio_pub_key_get;
	vox->kcontrol_new[ctl_id].put = vox_ctrl_dummy;
	vox->kcontrol_new[ctl_id].private_value =
				(unsigned long) &vox->s_bytes_bio_pub_key;
	vox->kcontrol_new[ctl_id].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
					   SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ctl_id++;
	vox->barge_in_status = VOX_BARGE_IN_DISABLED;

	vox->soc_enum_barge_in.items = VOX_NUM_BARGE_IN;
	vox->soc_enum_barge_in.texts = vox_barge_in_text;
	vox->soc_enum_barge_in.dobj.private = &vox->barge_in_status;
	vox_ctrl_enum_helper(&vox->kcontrol_new[ctl_id],
			     "Vox Barge-In",
			     (unsigned long) &vox->soc_enum_barge_in);
	vox->kcontrol_new[ctl_id].put = vox_ctrl_barge_in_put;

	ctl_id++;
	vox->bin_id = CLSIC_VOX_BIN_VTE1;

	memset(&vox->bin_id_mixer_ctrl, 0, sizeof(vox->bin_id_mixer_ctrl));
	vox->bin_id_mixer_ctrl.min = 0;
	vox->bin_id_mixer_ctrl.max = CLSIC_VOX_BIN_CNT - 1;
	vox->bin_id_mixer_ctrl.platform_max = CLSIC_VOX_BIN_CNT - 1;
	vox->bin_id_mixer_ctrl.dobj.private = &vox->bin_id;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Bin ID",
			    (unsigned long) &vox->bin_id_mixer_ctrl);

	ctl_id++;
	vox->asset_type = VOX_ASSET_TYPE_PHRASE;

	if (handler->service_version <= CLSIC_VOX_SRV_VERSION_MVP2)
		vox->soc_enum_asset_type.items = VOX_NUM_ASSET_TYPES_MVP2;
	else
		vox->soc_enum_asset_type.items = VOX_NUM_ASSET_TYPES_MVP;
	vox->soc_enum_asset_type.texts = vox_asset_type_text_mvp;

	vox->soc_enum_asset_type.dobj.private = &vox->asset_type;
	vox_ctrl_enum_helper(&vox->kcontrol_new[ctl_id],
			     "Vox Asset Type",
			     (unsigned long) &vox->soc_enum_asset_type);

	ctl_id++;
	vox->file_id = 0;

	memset(&vox->file_id_mixer_ctrl, 0, sizeof(vox->file_id_mixer_ctrl));
	vox->file_id_mixer_ctrl.min = 0;
	vox->file_id_mixer_ctrl.max = INT_MAX;
	vox->file_id_mixer_ctrl.platform_max = INT_MAX;
	vox->file_id_mixer_ctrl.dobj.private = &vox->file_id;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Asset Filename ID",
			    (unsigned long) &vox->file_id_mixer_ctrl);

	ctl_id++;
	vox->trigger_phrase_id = VOX_TRGR_INVALID;

	memset(&vox->trgr_phrase_id_mixer_ctrl, 0,
	       sizeof(vox->trgr_phrase_id_mixer_ctrl));
	vox->trgr_phrase_id_mixer_ctrl.min = INT_MIN;
	vox->trgr_phrase_id_mixer_ctrl.max = INT_MAX;
	vox->trgr_phrase_id_mixer_ctrl.platform_max = INT_MAX;
	vox->trgr_phrase_id_mixer_ctrl.dobj.private = &vox->trigger_phrase_id;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Trigger Phrase ID",
			    (unsigned long) &vox->trgr_phrase_id_mixer_ctrl);
	vox->kcontrol_new[ctl_id].put = vox_ctrl_dummy;

	ctl_id++;
	vox->trigger_engine_id = VOX_TRGR_INVALID;

	memset(&vox->trgr_engine_id_mixer_ctrl, 0,
	       sizeof(vox->trgr_engine_id_mixer_ctrl));
	vox->trgr_engine_id_mixer_ctrl.min = INT_MIN;
	vox->trgr_engine_id_mixer_ctrl.max = INT_MAX;
	vox->trgr_engine_id_mixer_ctrl.platform_max = INT_MAX;
	vox->trgr_engine_id_mixer_ctrl.dobj.private = &vox->trigger_engine_id;
	vox_ctrl_int_helper(&vox->kcontrol_new[ctl_id],
			    "Vox Trigger Engine ID",
			    (unsigned long) &vox->trgr_engine_id_mixer_ctrl);
	vox->kcontrol_new[ctl_id].put = vox_ctrl_dummy;

	ctl_id++;
	vox_ctrl_scc_helper(&vox->kcontrol_new[ctl_id],
			    "Vox SCCMANAGEACKCTRL",
			    &vox->s_bytes_scc_manage_ack,
			    vox);

	ctl_id++;
	vox->scc_status = 0;
	vox_ctrl_scc_helper(&vox->kcontrol_new[ctl_id],
			    "Vox SCC_STATUS",
			    &vox->s_bytes_scc_status,
			    vox);

	ctl_id++;
	vox->scc_cap_delay_ms = 0;
	vox_ctrl_scc_helper(&vox->kcontrol_new[ctl_id],
			    "Vox VTE1_CAPDELAYMS",
			    &vox->s_bytes_scc_cap_delay_ms,
			    vox);

	ctl_id++;
	vox->scc_triggerpoint = 0;
	vox_ctrl_scc_helper(&vox->kcontrol_new[ctl_id],
			    "Vox VTE1_TRIGGERPOINT",
			    &vox->s_bytes_scc_triggerpoint,
			    vox);

	ctl_id++;
	vox->scc_cap_preamble_ms = 0;
	vox_ctrl_scc_helper(&vox->kcontrol_new[ctl_id],
			    "Vox VTE1_CAPPREAMBLEMS",
			    &vox->s_bytes_scc_cap_preamble_ms,
			    vox);

	ctl_id++;
	vox_ctrl_scc_helper(&vox->kcontrol_new[ctl_id],
			    "Vox VTE1_PHRASEID",
			    &vox->s_bytes_scc_phraseid,
			    vox);

	BUG_ON(VOX_NUM_NEW_KCONTROLS != (ctl_id + 1));

	ret = snd_soc_add_codec_controls(codec, vox->kcontrol_new,
					 VOX_NUM_NEW_KCONTROLS);
	if (ret != 0) {
		pr_err("enum %s() add ret: %d.\n", __func__, ret);
		return ret;
	}

	init_completion(&vox->new_bio_results_completion);

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);
	if (ret != 0)
		return ret;

	vox->error_info_kctrl = snd_soc_card_get_kcontrol(
						vox->codec->component.card,
						"Vox Error Info");

	handler->data = (void *) vox;
	handler->callback = &vox_notification_handler;

	return ret;
}

/**
 * clsic_vox_codec_remove() - remove function for the codec part of the driver
 * @codec:	The main shared instance of struct snd_soc_codec used in
 *		CLSIC.
 *
 * Cancel any schedule work.
 *
 * Return: errno.
 */
static int clsic_vox_codec_remove(struct snd_soc_codec *codec)
{
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s() %p %p.\n", __func__, codec, vox);

	cancel_work_sync(&vox->drv_state_work);

	return 0;
}

static const struct snd_soc_codec_driver soc_codec_dev_clsic_vox = {
	.probe = &clsic_vox_codec_probe,
	.remove = &clsic_vox_codec_remove,
};

/**
 * clsic_vox_probe() - probe function for the module
 * @pdev:	Platform device struct.
 *
 * Standard module probe function.
 *
 * Return: errno.
 */
static int clsic_vox_probe(struct platform_device *pdev)
{
	struct clsic *clsic = dev_get_drvdata(pdev->dev.parent);
	struct clsic_service *vox_service = dev_get_platdata(&pdev->dev);
	struct clsic_vox *vox;
	int ret;

	vox = devm_kzalloc(&pdev->dev, sizeof(struct clsic_vox), GFP_KERNEL);
	if (vox == NULL)
		return -ENOMEM;

	vox->clsic = clsic;
	vox->service = clsic->service_handlers[vox_service->service_instance];
	vox->service->supports_debuginfo = true;

	platform_set_drvdata(pdev, vox);

	ret = snd_soc_register_platform(&pdev->dev, &clsic_vox_compr_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register platform: %d.\n", ret);
		return ret;
	}

	ret = snd_soc_register_codec(&pdev->dev, &soc_codec_dev_clsic_vox,
				     clsic_vox_dai, ARRAY_SIZE(clsic_vox_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register codec: %d.\n", ret);
		snd_soc_unregister_platform(&pdev->dev);
		return ret;
	}

#ifdef CONFIG_DEBUG_FS
	vox->debugfs_vox = debugfs_create_dir("vox", clsic->debugfs_root);

	vox->last_trigger.blob.data = &vox->last_trigger;
	vox->last_trigger.blob.size = 0;
	debugfs_create_blob("last_trigger", 0440, vox->debugfs_vox,
			    &vox->last_trigger.blob);

	vox->last_auth.blob.data = &vox->last_auth;
	vox->last_auth.blob.size = 0;
	debugfs_create_blob("last_auth", 0440, vox->debugfs_vox,
			    &vox->last_auth.blob);
	debugfs_create_u8("last_auth_lvl", 0440, vox->debugfs_vox,
			  &vox->last_auth.security_lvl);
	debugfs_create_u8("last_auth_fmt", 0440, vox->debugfs_vox,
			  &vox->last_auth.result_format);
#endif

	dev_info(&pdev->dev, "%s() Register: %p ret %d.\n", __func__,
		 &pdev->dev, ret);

	return ret;
}

/**
 * clsic_vox_remove() - remove function for the module
 * @pdev:	Platform device struct.
 *
 * Standard module remove function.
 *
 * Return: errno.
 */
static int clsic_vox_remove(struct platform_device *pdev)
{
	struct clsic_vox *vox = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s() dev %p priv %p.\n",
		 __func__, &pdev->dev, vox);

	if (vox->clsic_mode != CLSIC_VOX_MODE_IDLE)
		return -EBUSY;

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(vox->debugfs_vox);
#endif

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_codec(&pdev->dev);

	return 0;
}

static const struct of_device_id clsic_vox_of_match[] = {
	{ .compatible = "cirrus,clsic-vox", },
	{},
};
MODULE_DEVICE_TABLE(of, clsic_vox_of_match);

static struct platform_driver clsic_vox_driver = {
	.driver = {
		.name = "clsic-vox",
		.owner = THIS_MODULE,
		.of_match_table = clsic_vox_of_match,
	},
	.probe = &clsic_vox_probe,
	.remove = &clsic_vox_remove,
};

module_platform_driver(clsic_vox_driver);

MODULE_DESCRIPTION("ASoC Cirrus Logic CLSIC vox codec");
MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.wolfsonmicro.com>");
MODULE_AUTHOR("Ralph Clark <ralph.clark@cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clsic-vox");
