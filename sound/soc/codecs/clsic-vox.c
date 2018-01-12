/*
 * clsic-vox.c -- ALSA SoC CLSIC VOX
 *
 * Copyright 2017 CirrusLogic, Inc.
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
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/voxsrv.h>
#include <linux/mfd/clsic/irq.h>

#include "clsic-vox.h"

/* TODO: may require tuning */
#define VOX_ASR_MIN_FRAGMENT_SZ	0
#define VOX_ASR_MAX_FRAGMENT_SZ	307200
#define VOX_ASR_MIN_FRAGMENTS	4
#define VOX_ASR_MAX_FRAGMENTS	256

#define VOX_MAX_PHRASES		5

#define VOX_NUM_NEW_KCONTROLS	2

struct clsic_asr_stream_buf {
	void *data;

	size_t read_idx;
	size_t write_idx;

	size_t size;
	size_t frag_sz;
};

struct clsic_asr_stream {
	struct clsic_asr_stream_buf buf;

	struct snd_compr_stream *stream;

	unsigned int block_sz;
	unsigned int copied_total;

	unsigned int sample_rate;

	bool error;

	struct task_struct *wait_for_trigger;
	struct completion trigger_heard;
};

struct clsic_vox {
	struct clsic *clsic;
	struct clsic_service *service;
	struct snd_soc_codec *codec;

	/* ASR data stream */
	struct clsic_asr_stream asr_stream;

	/* The trigger detect callback */
	void (*trig_det_cb)(struct clsic *clsic, struct clsic_service *service);

	struct snd_kcontrol_new kcontrol_new[VOX_NUM_NEW_KCONTROLS];
	struct mutex mgmt_mode_lock;
	int mgmt_mode;
	int error_info;

	struct soc_enum soc_enum_mode;
	struct soc_enum soc_enum_error_info;
};

static const struct {
	u32 id;
	struct snd_codec_desc desc;
} clsic_asr_stream_caps = {
	.id = SND_AUDIOCODEC_PCM,
	.desc = {
		.max_ch = 2,
		.sample_rates = { 16000 },
		.num_sample_rates = 1,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

#define VOX_NUM_MGMT_MODES			10

#define VOX_MGMT_MODE_NEUTRAL			0
#define VOX_MGMT_MODE_INSTALL_PHRASE		1
#define VOX_MGMT_MODE_INSTALLING_PHRASE		2
#define VOX_MGMT_MODE_UNINSTALL_PHRASE		3
#define VOX_MGMT_MODE_UNINSTALLING_PHRASE	4
#define VOX_MGMT_MODE_REMOVE_USER		5
#define VOX_MGMT_MODE_REMOVING_USER		6
#define VOX_MGMT_MODE_START_USER_ENROLMENT	7
#define VOX_MGMT_MODE_STARTING_USER_ENROLMENT	8
#define VOX_MGMT_MODE_ENROL_STARTED		9

static const char *vox_mgmt_mode_text[VOX_NUM_MGMT_MODES] = {
	[VOX_MGMT_MODE_NEUTRAL]			= "Neutral",
	[VOX_MGMT_MODE_INSTALL_PHRASE]		= "Install Phrase",
	[VOX_MGMT_MODE_INSTALLING_PHRASE]	= "Installing Phrase",
	[VOX_MGMT_MODE_UNINSTALL_PHRASE]	= "Uninstall Phrase",
	[VOX_MGMT_MODE_UNINSTALLING_PHRASE]	= "Uninstalling Phrase",
	[VOX_MGMT_MODE_REMOVE_USER]		= "Remove User",
	[VOX_MGMT_MODE_REMOVING_USER]		= "Removing User",
	[VOX_MGMT_MODE_START_USER_ENROLMENT]	= "Start User Enrolment",
	[VOX_MGMT_MODE_STARTING_USER_ENROLMENT]	= "Starting User Enrolment",
	[VOX_MGMT_MODE_ENROL_STARTED]		= "Started User Enrolment",
};

#define VOX_NUM_ERRORS			10

#define VOX_ERROR_SUCCESS		0
#define VOX_ERROR_LIBRARY		1
#define VOX_ERROR_PROTOCOL		2
#define VOX_ERROR_TIMEOUT		3
#define VOX_ERROR_BAD_BPB		4
#define VOX_ERROR_DISABLE_BARGE_IN	5
#define VOX_ERROR_MORE_SPEECH_NEEDED	6
#define VOX_ERROR_TOO_LOUD		7
#define VOX_ERROR_TOO_NOISY		8
#define VOX_ERROR_CLEARED		9

static const char *vox_error_info_text[VOX_NUM_ERRORS] = {
	[VOX_ERROR_SUCCESS]		= "Success",
	[VOX_ERROR_LIBRARY]		= "Library",
	[VOX_ERROR_PROTOCOL]		= "Protocol",
	[VOX_ERROR_TIMEOUT]		= "Timeout",
	[VOX_ERROR_BAD_BPB]		= "Bad BPB File",
	[VOX_ERROR_DISABLE_BARGE_IN]	= "Barge-in Must Be Disabled",
	[VOX_ERROR_MORE_SPEECH_NEEDED]	= "More Speech Needed",
	[VOX_ERROR_TOO_LOUD]		= "Too Loud",
	[VOX_ERROR_TOO_NOISY]		= "Too Noisy",
	[VOX_ERROR_CLEARED]		= "Cleared",
};

/* Present method of phrase installation uses a fixed list of files. */
static struct {
	const char *file;
} phrase_files[VOX_MAX_PHRASES]	= {
	[CLSIC_VOX_PHRASE_VDT1]	= { .file = "bpb.p00" },
	[CLSIC_VOX_PHRASE_TI]	= { .file = "bpb.p04" },
};

/*
 * This lookup function is necessary because the CLSIC error codes are not
 * sequential. i.e. the error code is not necessarily equal to the array offset.
 */
static const char *clsic_error_string(int error_index)
{
	int array_size = sizeof(vega_response_codes) /
			 sizeof(struct vega_response_codes_struct);
	int i;

	for (i = 0; i < array_size; i++)
		if (vega_response_codes[i].code == error_index)
			return vega_response_codes[i].name;

	return "Unrecognised CLSIC error code";
}

int clsic_vox_asr_stream_open(struct clsic_vox *vox,
			      struct snd_compr_stream *stream)
{
	/*
	 * find vox service handler
	 *
	 * FIXME: for now an assumption is made that there is only one vox
	 *        service, which may not be the case in the future; to solve
	 *        this each instance of the vox service should be represented
	 *        by its own codec device
	 */

	if (vox->asr_stream.stream) {
		clsic_err(vox->clsic, "ASR stream already active.\n");
		return -EBUSY;
	}

	if (stream->direction != SND_COMPRESS_CAPTURE) {
		clsic_err(vox->clsic,
			  "Only capture is supported for ASR stream.\n");
		return -EINVAL;
	}

	vox->asr_stream.stream = stream;
	vox->asr_stream.error = false;
	vox->asr_stream.copied_total = 0;

	stream->runtime->private_data = &vox->asr_stream;

	trace_clsic_vox_asr_stream_open(stream->direction);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_open);

static int clsic_codec_asr_stream_open(struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(rtd->codec);

	if (strcmp(rtd->codec_dai->name, "clsic-dsp-vox-asr") != 0) {
		clsic_err(vox->clsic,
			  "No compressed stream supported for: %s\n",
			  rtd->codec_dai->name);
		return -EINVAL;
	}

	return clsic_vox_asr_stream_open(vox, stream);
}

int clsic_vox_asr_stream_free(struct snd_compr_stream *stream)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);

	trace_clsic_vox_asr_stream_free(stream->direction,
					asr_stream->copied_total);

	kfree(asr_stream->buf.data);
	asr_stream->buf.size = 0;
	asr_stream->buf.frag_sz = 0;
	asr_stream->buf.read_idx = 0;
	asr_stream->buf.write_idx = 0;

	asr_stream->copied_total = 0;
	asr_stream->stream = NULL;
	complete(&asr_stream->trigger_heard);
	vox->trig_det_cb = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_free);

static int clsic_vox_asr_stream_block_sz(u32 block_size)
{
	switch (block_size) {
	case 0:
		return CLSIC_VOX_ASR_BLK_SZ_0;
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
		break;
	}

	return -EINVAL;
}

#define PCM_S16_LE_BYTES_PER_SAMPLE 2

int clsic_vox_asr_stream_set_params(struct snd_compr_stream *stream,
				    struct snd_compr_params *params)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	size_t size, frag_sz = params->buffer.fragment_size;
	int block_sz, i, frame_sz;
	bool params_ok = true;

	frame_sz = params->codec.ch_in * PCM_S16_LE_BYTES_PER_SAMPLE;
	if (frag_sz % frame_sz) {
		clsic_err(clsic,
			  "%u is not a supported ASR stream fragment size.\n",
			  params->buffer.fragment_size);
		return -EINVAL;
	}
	block_sz = clsic_vox_asr_stream_block_sz(frag_sz / frame_sz);
	if (block_sz < 0) {
		clsic_err(clsic,
			  "%u is not a supported ASR stream fragment size.\n",
			  params->buffer.fragment_size);
		return -EINVAL;
	}

	params_ok = params_ok && (clsic_asr_stream_caps.id == params->codec.id);
	params_ok = params_ok &&
		(clsic_asr_stream_caps.desc.max_ch == params->codec.ch_in);
	params_ok = params_ok && (clsic_asr_stream_caps.desc.formats &
		(1 << params->codec.format));
	for (i = 0; i < clsic_asr_stream_caps.desc.num_sample_rates; ++i)
		if (clsic_asr_stream_caps.desc.sample_rates[i] ==
				params->codec.sample_rate)
			break;
	params_ok = params_ok &&
		(i < clsic_asr_stream_caps.desc.num_sample_rates);

	if (!params_ok) {
		clsic_err(clsic,
			  "Invalid params id=%u, ch=%u,%u, rate=%u fmt=%u\n",
			  params->codec.id, params->codec.ch_in,
			  params->codec.ch_out, params->codec.sample_rate,
			  params->codec.format);
		return -EINVAL;
	}

	asr_stream->block_sz = block_sz;

	size = frag_sz * params->buffer.fragments;
	asr_stream->buf.data = kmalloc(size, GFP_KERNEL);
	if (!asr_stream->buf.data)
		return -ENOMEM;
	asr_stream->buf.size = size;

	trace_clsic_vox_asr_stream_set_params(params, size);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_set_params);

int clsic_vox_asr_stream_get_params(struct snd_compr_stream *stream,
				    struct snd_codec *params)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_get_params);

static enum clsic_message_cb_ret clsic_vox_asr_stream_data_cb(
						      struct clsic *clsic,
						      struct clsic_message *msg)
{
	struct clsic_service *handler =
		clsic_find_first_service(clsic, CLSIC_SRV_TYPE_VOX);
	struct clsic_vox *vox = handler->data;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;
	union clsic_vox_msg *msg_rsp;
	size_t read_idx, write_idx;
	u32 payload_sz;

	if (!asr_stream->stream) {
		clsic_dbg(clsic, "ASR stream is no longer active.\n");
		return CLSIC_MSG_RELEASED;
	}

	msg_rsp = (union clsic_vox_msg *) &msg->response;
	if (!clsic_get_bulk_bit(msg_rsp->rsp_get_asr_block.hdr.sbc) &&
	    msg_rsp->rsp_get_asr_block.hdr.err != 0) {
		clsic_err(clsic,
			  "Device responded with error code: %d\n",
			  msg_rsp->rsp_get_asr_block.hdr.err);
		asr_stream->error = true;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	} else if (msg_rsp->blkrsp_get_asr_block.hdr.err != 0) {
		clsic_err(clsic,
			  "Device responded with error code: %d\n",
			  msg_rsp->blkrsp_get_asr_block.hdr.err);
		asr_stream->error = true;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	}

	write_idx = asr_stream->buf.write_idx;
	read_idx = READ_ONCE(asr_stream->buf.read_idx);
	payload_sz = msg_rsp->blkrsp_get_asr_block.hdr.bulk_sz;

	trace_clsic_vox_asr_stream_data_rcv_start(payload_sz, read_idx,
						  write_idx);

	if ((read_idx - (write_idx + 1)) % asr_stream->buf.size >=
	    asr_stream->buf.frag_sz) {
		/* extract data from the response to an intermediate buffer */
		memcpy((u8 *) asr_stream->buf.data + write_idx,
		       msg->bulk_rxbuf,
		       payload_sz);

		smp_store_release(&asr_stream->buf.write_idx,
				  (write_idx + payload_sz) %
				  (asr_stream->buf.size));

		asr_stream->copied_total += payload_sz;

		/* notify the compressed framework of available data */
		snd_compr_fragment_elapsed(asr_stream->stream);

		trace_clsic_vox_asr_stream_data_rcv_end(payload_sz,
						    asr_stream->buf.read_idx,
						    asr_stream->buf.write_idx);
	} else {
		clsic_err(clsic, "ASR stream overflow.\n");
		asr_stream->error = true;
		asr_stream->copied_total += payload_sz;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	}

	return CLSIC_MSG_RELEASED;
}

static void clsic_vox_asr_stream_trig_det_cb(struct clsic *clsic,
					     struct clsic_service *service)
{
	struct clsic_vox *vox = service->data;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;

	trace_clsic_vox_trigger_heard(service->service_instance);

	if (asr_stream->stream)
		complete(&asr_stream->trigger_heard);
}

static int clsic_vox_asr_stream_wait_for_trigger(void *data)
{
	struct clsic_asr_stream *asr_stream = data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	int ret = 0;

	if (wait_for_completion_interruptible(&asr_stream->trigger_heard)) {
		clsic_dbg(clsic, "Wait for ASR stream trigger aborted.\n");

		if (asr_stream->stream) {
			/* force compressed fw to notice error */
			asr_stream->error = true;
			asr_stream->copied_total += 1;
			snd_compr_fragment_elapsed(asr_stream->stream);
		}
		return 0;
	}

	if (!asr_stream->stream)
		return 0;

	trace_clsic_vox_asr_stream_data_start(asr_stream->copied_total);

	/* queue up the first read */
	clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);

	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   0,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		clsic_err(clsic, "Error sending msg: %d\n", ret);
		/* force compressed fw to notice error */
		asr_stream->error = true;
		asr_stream->copied_total += 1;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return 0;
	}

	trace_clsic_vox_asr_stream_queue_read(asr_stream->copied_total);

	return 0;
}

int clsic_vox_asr_stream_trigger(struct snd_compr_stream *stream, int cmd)
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
		/* instruct the service to enter listen mode */
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_IDLE;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter idle mode: %d\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}

		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_LISTEN;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter listen mode: %d\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}

		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_LISTEN_START);
		/* TODO: add handling for external trigger */
		msg_cmd.cmd_listen_start.trgr_domain =
			CLSIC_VOX_TRIG_DOMAIN_INTRNL;
		msg_cmd.cmd_listen_start.asr_blk_sz = asr_stream->block_sz;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_listen_start.hdr.err) {
			clsic_err(clsic,
				  "Failed to start listening: %d\n",
				  msg_rsp.rsp_listen_start.hdr.err);
			return -EIO;
		}

		trace_clsic_vox_asr_stream_listen(
					  msg_cmd.cmd_listen_start.trgr_domain);

		reinit_completion(&asr_stream->trigger_heard);

		asr_stream->wait_for_trigger =
			kthread_create(clsic_vox_asr_stream_wait_for_trigger,
				       asr_stream,
				       "clsic-vox-asr-wait-for-trigger");

		vox->trig_det_cb = clsic_vox_asr_stream_trig_det_cb;

		wake_up_process(asr_stream->wait_for_trigger);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_IDLE;

		ret = clsic_send_msg_sync(
				     clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
		if (ret) {
			clsic_err(clsic, "Error sending msg: %d\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter idle mode: %d\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_trigger);

int clsic_vox_asr_stream_pointer(struct snd_compr_stream *stream,
				 struct snd_compr_tstamp *tstamp)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;

	tstamp->copied_total = asr_stream->copied_total;
	tstamp->sampling_rate = asr_stream->sample_rate;

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_pointer);

int clsic_vox_asr_stream_copy(struct snd_compr_stream *stream, char __user *buf,
			      size_t count)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;
	struct clsic_vox *vox =
		container_of(asr_stream, struct clsic_vox, asr_stream);
	struct clsic *clsic = vox->clsic;
	union clsic_vox_msg msg_cmd;
	size_t write_idx, read_idx;
	int ret = 0;

	if (asr_stream->error) {
		clsic_err(clsic, "ASR stream xrun.\n");
		return -EIO;
	}

	write_idx = smp_load_acquire(&asr_stream->buf.write_idx);
	read_idx = asr_stream->buf.read_idx;

	trace_clsic_vox_asr_stream_copy_start(count, read_idx, write_idx);

	if ((write_idx - read_idx) % asr_stream->buf.size >= count) {
		if (copy_to_user(buf, (uint8_t *) asr_stream->buf.data +
						read_idx, count)) {
			clsic_err(clsic, "Failed to copy data to user.\n");
			return -EFAULT;
		}

		smp_store_release(&asr_stream->buf.read_idx,
				  (read_idx + count) %
				  (asr_stream->buf.size));

		trace_clsic_vox_asr_stream_copy_end(count,
						    asr_stream->buf.read_idx,
						    asr_stream->buf.write_idx);
	} else {
		/*
		 * underrun - should never happen as the stream will be
		 * signalled only when there is data available or the stream
		 * has overran
		 */
		clsic_err(clsic, "ASR stream underrun.\n");
		return -EIO;
	}

	/* queue up next read */
	clsic_init_message((union t_clsic_generic_message *)&msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);

	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   0,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		clsic_err(clsic, "Error sending msg: %d\n", ret);
		return -EIO;
	}

	trace_clsic_vox_asr_stream_queue_read(asr_stream->copied_total);

	return count;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_copy);

int clsic_vox_asr_stream_get_caps(struct snd_compr_stream *stream,
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
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_get_caps);

int clsic_vox_asr_stream_get_codec_caps(struct snd_compr_stream *stream,
					struct snd_compr_codec_caps *codec)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_get_codec_caps);

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

static struct snd_compr_ops clsic_vox_compr_ops = {
	.open = clsic_codec_asr_stream_open,
	.free = clsic_vox_asr_stream_free,
	.set_params = clsic_vox_asr_stream_set_params,
	.trigger = clsic_vox_asr_stream_trigger,
	.pointer = clsic_vox_asr_stream_pointer,
	.copy = clsic_vox_asr_stream_copy,
	.get_caps = clsic_vox_asr_stream_get_caps,
};

static struct snd_soc_platform_driver clsic_vox_compr_platform = {
	.compr_ops = &clsic_vox_compr_ops,
};

static const char *vox_clsic_mode_strings[6] = {
	[CLSIC_VOX_MODE_IDLE]	= "IDLE",
	[CLSIC_VOX_MODE_MANAGE]	= "MANAGE",
	[CLSIC_VOX_MODE_ENROL]	= "ENROL",
	[CLSIC_VOX_MODE_LISTEN] = "LISTEN",
	[CLSIC_VOX_MODE_STREAM] = "STREAM",
};

static int vox_set_mode(struct clsic_vox *vox, enum clsic_vox_mode new_mode)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_SET_MODE);
	msg_cmd.cmd_set_mode.mode = new_mode;

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	clsic_info(vox->clsic, "ret %d new mode %s.\n", ret,
		   vox_clsic_mode_strings[new_mode]);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	switch (msg_rsp.rsp_set_mode.hdr.err) {
	case CLSIC_ERR_NONE:
		return 0;
	case CLSIC_ERR_INVAL_MODE_TRANSITION:
	case CLSIC_ERR_INVAL_MODE:
		clsic_err(vox->clsic, "%s\n",
			  clsic_error_string(msg_rsp.rsp_set_mode.hdr.err));
		return -EIO;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_set_mode.hdr.err,
			  clsic_error_string(msg_rsp.rsp_set_mode.hdr.err));
		return -EIO;
	}
}

static int vox_ctrl_error_info_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_error_info);

	ucontrol->value.enumerated.item[0] = vox->error_info;

	return 0;
}

static int vox_ctrl_error_info_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_error_info);

	if (ucontrol->value.enumerated.item[0] != VOX_ERROR_CLEARED)
		return -EINVAL;

	vox->error_info = ucontrol->value.enumerated.item[0];

	return 0;
}

static int vox_ctrl_mgmt_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_mode);

	ucontrol->value.enumerated.item[0] = vox->mgmt_mode;

	return 0;
}

static int vox_ctrl_mgmt_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_mode);
	int ret = 0;

	if (ucontrol->value.enumerated.item[0] == vox->mgmt_mode)
		return 0;

	if (ucontrol->value.enumerated.item[0] >= VOX_NUM_MGMT_MODES)
		return -EINVAL;

	switch (ucontrol->value.enumerated.item[0]) {
	case VOX_MGMT_MODE_NEUTRAL:
		mutex_lock(&vox->mgmt_mode_lock);
		ret = vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);
		if (ret) {
			mutex_unlock(&vox->mgmt_mode_lock);
			clsic_err(vox->clsic, "%s: %d.\n", __func__, ret);
			return ret;
		}
		vox->mgmt_mode = VOX_MGMT_MODE_NEUTRAL;
		mutex_unlock(&vox->mgmt_mode_lock);
		clsic_info(vox->clsic, "vox mode set to neutral.\n");
		break;
	default:
		ret = -EINVAL;
		clsic_err(vox->codec, "unrecognised vox mode %d.\n",
			  ucontrol->value.enumerated.item[0]);
	}

	return ret;
}

static int vox_notification_handler(struct clsic *clsic,
				    struct clsic_service *handler,
				    struct clsic_message *msg)
{
	struct clsic_vox *vox = (struct clsic_vox *) handler->data;
	enum clsic_vox_msg_id msgid;
	union clsic_vox_msg *msg_rsp = (union clsic_vox_msg *) &msg->response;
	int ret = CLSIC_UNHANDLED;

	/* Make sure it is a notification message. */
	if (clsic_get_cran_frommsg(msg) != CLSIC_CRAN_NTY)
		return ret;

	msgid = clsic_get_messageid(msg);
	switch (msgid) {
	case CLSIC_VOX_MSG_N_LISTEN_ERR:
		/* TODO: should we be doing something more than this here? */
		clsic_err(vox->clsic, "trigger detection error on CLSIC.\n");
		ret = CLSIC_HANDLED;
		break;
	case CLSIC_VOX_MSG_N_TRGR_DETECT:
		if (vox->trig_det_cb)
			vox->trig_det_cb(vox->clsic,
				clsic_find_first_service(vox->clsic,
							 CLSIC_SRV_TYPE_VOX));
		ret = CLSIC_HANDLED;
		break;
	default:
		clsic_err(clsic, "unrecognised message with message ID %d\n",
			  msgid);
	}

	return ret;
}

static int clsic_vox_codec_probe(struct snd_soc_codec *codec)
{
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(codec);
	struct clsic_service *handler =
		clsic_find_first_service(vox->clsic, CLSIC_SRV_TYPE_VOX);
	int ret;

	dev_info(codec->dev, "%s() %p.\n", __func__, codec);

	vox->codec = codec;

	init_completion(&vox->asr_stream.trigger_heard);

	vox->mgmt_mode = VOX_MGMT_MODE_NEUTRAL;
	mutex_init(&vox->mgmt_mode_lock);

	vox->kcontrol_new[0].name = "Vox Management Mode";
	vox->kcontrol_new[0].info = snd_soc_info_enum_double;
	vox->kcontrol_new[0].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[0].get = vox_ctrl_mgmt_get;
	vox->kcontrol_new[0].put = vox_ctrl_mgmt_put;
	vox->soc_enum_mode.items = VOX_NUM_MGMT_MODES;
	vox->soc_enum_mode.texts = vox_mgmt_mode_text;
	vox->kcontrol_new[0].private_value =
					(unsigned long) &vox->soc_enum_mode;
	vox->kcontrol_new[0].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->error_info = VOX_ERROR_CLEARED;

	vox->kcontrol_new[1].name = "Vox Error Info";
	vox->kcontrol_new[1].info = snd_soc_info_enum_double;
	vox->kcontrol_new[1].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[1].get = vox_ctrl_error_info_get;
	vox->kcontrol_new[1].put = vox_ctrl_error_info_put;
	vox->soc_enum_error_info.items = VOX_NUM_ERRORS;
	vox->soc_enum_error_info.texts = vox_error_info_text;
	vox->kcontrol_new[1].private_value =
				(unsigned long)(&(vox->soc_enum_error_info));
	vox->kcontrol_new[1].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ret = snd_soc_add_codec_controls(codec, vox->kcontrol_new,
					 VOX_NUM_NEW_KCONTROLS);
	if (ret != 0) {
		pr_err("enum %s() add ret: %d.\n", __func__, ret);
		return ret;
	}

	handler->data = (void *)vox;
	handler->callback = &vox_notification_handler;

	return ret;
}

static int clsic_vox_codec_remove(struct snd_soc_codec *codec)
{
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s() %p %p.\n", __func__, codec, vox);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_clsic_vox = {
	.probe = clsic_vox_codec_probe,
	.remove = clsic_vox_codec_remove,
};

static int clsic_vox_probe(struct platform_device *pdev)
{
	struct clsic *clsic = dev_get_drvdata(pdev->dev.parent);
	struct clsic_service *vox_service = dev_get_platdata(&pdev->dev);
	struct clsic_vox *vox;
	int ret;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;

	dev_info(&pdev->dev, "%s() service %p.\n", __func__, vox_service);

	dev_info(&pdev->dev, "%s() clsic %p.\n", __func__, clsic);

	vox = devm_kzalloc(&pdev->dev, sizeof(struct clsic_vox),
				 GFP_KERNEL);
	if (vox == NULL)
		return -ENOMEM;

	vox->clsic = clsic;
	vox->service = vox_service;

	platform_set_drvdata(pdev, vox);
#if 0
	pm_runtime_enable(&pdev->dev);
	pm_runtime_idle(&pdev->dev);
#endif

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

	dev_info(&pdev->dev, "%s() Register: %p ret %d.\n", __func__,
		 &pdev->dev, ret);

	if (ret == 0) {
		dev_info(&pdev->dev, "%s() test sending idle message.\n",
			 __func__);

		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox_service->service_instance,
				   CLSIC_VOX_MSG_CR_SET_MODE);
		msg_cmd.cmd_set_mode.mode = CLSIC_VOX_MODE_IDLE;

		ret = clsic_send_msg_sync(clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

		dev_info(&pdev->dev, "%s() idle message %d %d.\n",
			 __func__, ret, msg_rsp.rsp_set_mode.hdr.err);

		if (ret) {
			clsic_err(clsic, "Error sending msg: %d.\n", ret);
			return -EIO;
		}
		if (msg_rsp.rsp_set_mode.hdr.err) {
			clsic_err(clsic,
				  "Failed to enter idle mode: %d.\n",
				  msg_rsp.rsp_set_mode.hdr.err);
			return -EIO;
		}
	}

	return ret;
}

static int clsic_vox_remove(struct platform_device *pdev)
{
	struct clsic_vox *vox = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s() dev %p priv %p.\n",
		 __func__, &pdev->dev, vox);

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_codec(&pdev->dev);

#if 0
	pm_runtime_disable(&pdev->dev);
#endif

	return 0;
}

static struct platform_driver clsic_vox_driver = {
	.driver = {
		.name = "clsic-vox",
		.owner = THIS_MODULE,
	},
	.probe = clsic_vox_probe,
	.remove = clsic_vox_remove,
};

module_platform_driver(clsic_vox_driver);

MODULE_DESCRIPTION("ASoC Cirrus Logic CLSIC VOX codec");
MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.wolfsonmicro.com>");
MODULE_AUTHOR("Ralph Clark <ralph.clark@cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clsic-vox");
