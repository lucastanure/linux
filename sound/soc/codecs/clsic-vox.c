/*
 * clsic-vox.c -- ALSA SoC CLSIC VOX
 *
 * Copyright 2018 CirrusLogic, Inc.
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
#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/irq.h>

#include "clsic-vox.h"

#define VOX_ASR_MIN_FRAGMENT_SZ	0
#define VOX_ASR_MAX_FRAGMENT_SZ	307200
#define VOX_ASR_MIN_FRAGMENTS	1
#define VOX_ASR_MAX_FRAGMENTS	256

#define VOX_MAX_USERS		3
#define VOX_MAX_PHRASES		5

#define VOX_NUM_NEW_KCONTROLS	20

#define CLSIC_ASSET_SIZE_ALIGNMENT	4

#define VOX_DEFAULT_DURATION		0
#define VOX_DEFAULT_TIMEOUT		4000
#define VOX_MAX_DURATION_TIMEOUT	0xFFFF
#define VOX_DEFAULT_NUM_REPS		3
#define VOX_MAX_NUM_REPS		5

#define CLSIC_VOX_SRV_VERSION_MVP2	0x00030002	/* 2.0.248 */

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
	bool listen_error;

	bool error;
	bool asr_block_pending;

	struct task_struct *wait_for_trigger;
	struct completion trigger_heard;
	struct completion asr_block_completion;
};

union bio_results_u {
	struct clsic_vox_auth_result result;
	struct clsic_vox_auth_result_ex result_ex;
	struct clsic_vox_auth_result_ex2 result_ex2;
};

struct clsic_vox {
	struct clsic *clsic;
	struct clsic_service *service;
	struct snd_soc_codec *codec;

	/* ASR data stream */
	struct clsic_asr_stream asr_stream;

	struct snd_kcontrol_new kcontrol_new[VOX_NUM_NEW_KCONTROLS];
	struct mutex mgmt_mode_lock;
	/* mgmt_mode refers to ongoing vox biometric operations only. */
	unsigned int mgmt_mode;
	/* error_info for showing result of a top level control mode change. */
	unsigned int error_info;
	unsigned int asset_type;

	unsigned int phrase_id;
	unsigned int user_id;
	unsigned int bin_id;
	unsigned int file_id;	/* For filename determination. */
	unsigned int duration;
	unsigned int timeout;
	unsigned int number_of_reps;
	unsigned int security_level;
	unsigned int bio_results_format;
	struct clsic_vox_auth_challenge challenge;
	union bio_results_u biometric_results;
	struct clsic_vox_auth_key bio_pub_key;
	bool get_bio_results_early_exit;
	uint8_t auth_error;
	/*
	 * asr_streaming tells us if we are currently streaming audio data -
	 * it is only possible to enter/exit this mode when not undertaking any
	 * vox biometric operations.
	 */
	int asr_strm_mode;
	unsigned int barge_in_status;
	int trigger_phrase_id;
	int trigger_engine_id;

	struct soc_enum soc_enum_mode;
	struct soc_enum soc_enum_error_info;
	struct soc_enum soc_enum_sec_level;
	struct soc_enum soc_enum_bio_res_type;
	struct soc_enum soc_enum_barge_in;
	struct soc_enum soc_enum_asset_type;
	struct soc_enum soc_enum_trgr_phr;
	struct soc_enum soc_enum_trgr_eng;

	struct soc_mixer_control phrase_id_mixer_ctrl;
	struct soc_mixer_control user_id_mixer_ctrl;
	struct soc_mixer_control duration_mixer_ctrl;
	struct soc_mixer_control timeout_mixer_ctrl;
	struct soc_mixer_control reps_mixer_ctrl;
	struct soc_mixer_control bin_id_mixer_ctrl;
	struct soc_mixer_control file_id_mixer_ctrl;

	struct soc_bytes_ext s_bytes_challenge;
	struct soc_bytes_ext s_bytes_bio_res;
	struct soc_bytes_ext s_bytes_bio_pub_key;

	bool phrase_installed[VOX_MAX_PHRASES];
	bool user_installed[VOX_MAX_PHRASES * VOX_MAX_USERS];
	bool bin_installed[CLSIC_VOX_BIN_CNT];
	bool bio_vte_map_installed;

	struct work_struct mgmt_mode_work;
	struct snd_kcontrol *mgmt_mode_kctrl;

	struct completion new_bio_results_completion;
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

/* asr_strm_mode */
#define VOX_ASR_MODE_INACTIVE			0
#define VOX_ASR_MODE_STARTING			1
#define VOX_ASR_MODE_STREAMING			2
#define VOX_ASR_MODE_STOPPING			3

#define VOX_NUM_MGMT_MODES			20

#define VOX_MGMT_MODE_NEUTRAL			0
#define VOX_MGMT_MODE_INSTALL_ASSET		1
#define VOX_MGMT_MODE_INSTALLING_ASSET		2
#define VOX_MGMT_MODE_UNINSTALL_ASSET		3
#define VOX_MGMT_MODE_UNINSTALLING_ASSET	4
#define VOX_MGMT_MODE_REMOVE_USER		5
#define VOX_MGMT_MODE_REMOVING_USER		6
#define VOX_MGMT_MODE_START_ENROL		7
#define VOX_MGMT_MODE_STARTING_ENROL		8
#define VOX_MGMT_MODE_STARTED_ENROL		9
#define VOX_MGMT_MODE_PERFORM_ENROL_REP		10
#define VOX_MGMT_MODE_PERFORMING_ENROL_REP	11
#define VOX_MGMT_MODE_COMPLETE_ENROL		12
#define VOX_MGMT_MODE_COMPLETING_ENROL		13
#define VOX_MGMT_MODE_TERMINATE_ENROL		14
#define VOX_MGMT_MODE_TERMINATING_ENROL		15
#define VOX_MGMT_MODE_GET_BIO_RESULTS		16
#define VOX_MGMT_MODE_GETTING_BIO_RESULTS	17
#define VOX_MGMT_MODE_STOP_BIO_RESULTS		18
#define VOX_MGMT_MODE_STOPPING_BIO_RESULTS	19

static const char *vox_mgmt_mode_text[VOX_NUM_MGMT_MODES] = {
	[VOX_MGMT_MODE_NEUTRAL]			= "Neutral",
	[VOX_MGMT_MODE_INSTALL_ASSET]		= "Install Asset",
	[VOX_MGMT_MODE_INSTALLING_ASSET]	= "Installing Asset",
	[VOX_MGMT_MODE_UNINSTALL_ASSET]		= "Uninstall Asset",
	[VOX_MGMT_MODE_UNINSTALLING_ASSET]	= "Uninstalling Asset",
	[VOX_MGMT_MODE_REMOVE_USER]		= "Remove User",
	[VOX_MGMT_MODE_REMOVING_USER]		= "Removing User",
	[VOX_MGMT_MODE_START_ENROL]		= "Start User Enrolment",
	[VOX_MGMT_MODE_STARTING_ENROL]		= "Starting User Enrolment",
	[VOX_MGMT_MODE_STARTED_ENROL]		= "Started User Enrolment",
	[VOX_MGMT_MODE_PERFORM_ENROL_REP] = "Perform Enrolment Repetition",
	[VOX_MGMT_MODE_PERFORMING_ENROL_REP] =
					    "Performing Enrolment Repetition",
	[VOX_MGMT_MODE_COMPLETE_ENROL]		= "Complete User Enrolment",
	[VOX_MGMT_MODE_COMPLETING_ENROL]	= "Completing User Enrolment",
	[VOX_MGMT_MODE_TERMINATE_ENROL]		= "Terminate User Enrolment",
	[VOX_MGMT_MODE_TERMINATING_ENROL]	= "Terminating User Enrolment",
	[VOX_MGMT_MODE_GET_BIO_RESULTS]		= "Get Biometric Results",
	[VOX_MGMT_MODE_GETTING_BIO_RESULTS]	= "Getting Biometric Results",
	[VOX_MGMT_MODE_STOP_BIO_RESULTS]	= "Stop Biometric Results",
	[VOX_MGMT_MODE_STOPPING_BIO_RESULTS]	= "Stopping Biometric Results",
};

#define VOX_NUM_ERRORS			11

#define VOX_ERROR_SUCCESS		0
#define VOX_ERROR_LIBRARY		1
#define VOX_ERROR_TIMEOUT		2
#define VOX_ERROR_BAD_ASSET		3
#define VOX_ERROR_DISABLE_BARGE_IN	4
#define VOX_ERROR_MORE_SPEECH_NEEDED	5
#define VOX_ERROR_TOO_LOUD		6
#define VOX_ERROR_TOO_NOISY		7
#define VOX_ERROR_NO_USERS		8
#define VOX_ERROR_BIO_TIME_EXCEEDED	9
#define VOX_ERROR_CLEARED		10

static const char *vox_error_info_text[VOX_NUM_ERRORS] = {
	[VOX_ERROR_SUCCESS]		= "Success",
	[VOX_ERROR_LIBRARY]		= "Library",
	[VOX_ERROR_TIMEOUT]		= "Timed Out",
	[VOX_ERROR_BAD_ASSET]		= "Bad Asset File",
	[VOX_ERROR_DISABLE_BARGE_IN]	= "Barge-in Must Be Disabled",
	[VOX_ERROR_MORE_SPEECH_NEEDED]	= "More Speech Needed",
	[VOX_ERROR_TOO_LOUD]		= "Too Loud",
	[VOX_ERROR_TOO_NOISY]		= "Too Noisy",
	[VOX_ERROR_NO_USERS]		= "No Users Identified",
	[VOX_ERROR_BIO_TIME_EXCEEDED]	= "Maximum Voice ID Duration Exceeded",
	[VOX_ERROR_CLEARED]		= "Cleared",
};

#define VOX_NUM_BIO_RESULTS_FORMATS	3

#define VOX_BIO_RESULTS_CLASSIC		0
#define VOX_BIO_RESULTS_EXT_V1		1
#define VOX_BIO_RESULTS_EXT_V2		2

static const char *vox_bio_results_format_text[VOX_NUM_BIO_RESULTS_FORMATS] = {
	[VOX_BIO_RESULTS_CLASSIC]	= "Classic",
	[VOX_BIO_RESULTS_EXT_V1]	= "Extended Version 1",
	[VOX_BIO_RESULTS_EXT_V2]	= "Extended Version 2",
};

#define VOX_NUM_SEC_LEVEL		3

#define VOX_SEC_LEVEL_LOW		0
#define VOX_SEC_LEVEL_MEDIUM		1
#define VOX_SEC_LEVEL_HIGH		2

static const char *vox_sec_level_text[VOX_NUM_SEC_LEVEL] = {
	[VOX_SEC_LEVEL_LOW]		= "Low",
	[VOX_SEC_LEVEL_MEDIUM]		= "Medium",
	[VOX_SEC_LEVEL_HIGH]		= "High",
};

#define VOX_NUM_BARGE_IN		2

#define VOX_BARGE_IN_DISABLED		0
#define VOX_BARGE_IN_ENABLED		1

static const char *vox_barge_in_text[VOX_NUM_BARGE_IN] = {
	[VOX_BARGE_IN_DISABLED]		= "Loudspeaker Disabled",
	[VOX_BARGE_IN_ENABLED]		= "Loudspeaker Enabled",
};

#define VOX_NUM_ASSET_TYPES_MVP2	1
#define VOX_NUM_ASSET_TYPES_MVP		4

#define VOX_ASSET_TYPE_PHRASE		0
#define VOX_ASSET_TYPE_BIN_VTE		1
#define VOX_ASSET_TYPE_BIN_SSF		2
#define VOX_ASSET_TYPE_BIO_VTE_MAP	3

static const char *vox_asset_type_text_mvp[VOX_NUM_ASSET_TYPES_MVP] = {
	[VOX_ASSET_TYPE_PHRASE]		= "Biometric Phrase",
	[VOX_ASSET_TYPE_BIN_VTE]	= "Voice Trigger Engine Bin",
	[VOX_ASSET_TYPE_BIN_SSF]	= "Start Stop Flagger Bin",
	[VOX_ASSET_TYPE_BIO_VTE_MAP]	= "Biometric Voice Trigger Engine Map",
};

/* Templates for asset filenames. */
static const char *vox_asset_filenames[VOX_NUM_ASSET_TYPES_MVP] = {
	[VOX_ASSET_TYPE_PHRASE]		= "bpb.p%02u",
	[VOX_ASSET_TYPE_BIN_VTE]	= "vte%u.bin",
	[VOX_ASSET_TYPE_BIN_SSF]	= "ssf%u.bin",
	[VOX_ASSET_TYPE_BIO_VTE_MAP]	= "biovte%u.map",
};
#define VOX_ASSET_TYPE_NAME_MAX_LEN	21

#define VOX_TRGR_INVALID		0

#define VOX_NUM_TRGR_ENG		2

#define VOX_TRGR_ENG_12			1
#define VOX_TRGR_ENG_12_NUM		12

static const char *vox_trgr_eng_text[VOX_NUM_TRGR_ENG] = {
	[VOX_TRGR_INVALID]		= "Invalid",
	[VOX_TRGR_ENG_12]		= "12",
};

#define VOX_NUM_TRGR_PHR		3

#define VOX_TRGR_PHR_1			1
#define VOX_TRGR_PHR_2			2

static const char *vox_trgr_phr_text[VOX_NUM_TRGR_PHR] = {
	[VOX_TRGR_INVALID]		= "Invalid",
	[VOX_TRGR_PHR_1]		= "1",
	[VOX_TRGR_PHR_2]		= "2",
};

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

static int clsic_vox_asr_stream_open(struct snd_compr_stream *stream)
{
	struct snd_soc_pcm_runtime *rtd = stream->private_data;
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(rtd->codec);

	if (strcmp(rtd->codec_dai->name, "clsic-dsp-vox-asr") != 0) {
		clsic_err(vox->clsic,
			  "No compressed stream supported for: %s\n",
			  rtd->codec_dai->name);
		return -EINVAL;
	}

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

int clsic_vox_asr_stream_free(struct snd_compr_stream *stream)
{
	struct clsic_asr_stream *asr_stream = stream->runtime->private_data;

	trace_clsic_vox_asr_stream_free(stream->direction,
					asr_stream->copied_total);

	kfree(asr_stream->buf.data);

	asr_stream->buf.data = NULL;
	asr_stream->buf.size = 0;
	asr_stream->buf.frag_sz = 0;
	asr_stream->buf.read_idx = 0;
	asr_stream->buf.write_idx = 0;

	asr_stream->copied_total = 0;
	asr_stream->stream = NULL;
	asr_stream->listen_error = true;
	complete(&asr_stream->trigger_heard);

	return 0;
}
EXPORT_SYMBOL_GPL(clsic_vox_asr_stream_free);

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
	size_t frag_sz = params->buffer.fragment_size;
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
	/* Avoid memory leaks from multiple calls to set_params. */
	kfree(asr_stream->buf.data);
	asr_stream->buf.data = kmalloc(frag_sz, GFP_KERNEL);
	if (!asr_stream->buf.data)
		return -ENOMEM;
	asr_stream->buf.size = frag_sz;

	trace_clsic_vox_asr_stream_set_params(params, frag_sz);

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
	struct clsic_vox *vox = (struct clsic_vox *) (uintptr_t) msg->cookie;
	struct clsic_asr_stream *asr_stream = &vox->asr_stream;
	union clsic_vox_msg *msg_rsp;
	u32 payload_sz;

	asr_stream->asr_block_pending = false;
	complete(&asr_stream->asr_block_completion);

	if (!asr_stream->stream) {
		clsic_dbg(clsic, "ASR stream is no longer active.\n");
		return CLSIC_MSG_RELEASED;
	}

	msg_rsp = (union clsic_vox_msg *) &msg->response;
	if (!clsic_get_bulk_bit(msg_rsp->rsp_get_asr_block.hdr.sbc) &&
	    msg_rsp->rsp_get_asr_block.hdr.err != 0) {
		/*
		 * Error CLSIC_ERR_CANCELLED simply means that we have set CLSIC
		 * to IDLE mode while there is a pending ASR request (see
		 * clsic_vox_asr_stream_trigger). This causes CLSIC to forcibly
		 * cancel the request for that ASR block.
		 */
		clsic_info(clsic, "response: %s\n",
			   clsic_error_string(
				msg_rsp->rsp_get_asr_block.hdr.err));
		asr_stream->error = true;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	} else if (msg_rsp->blkrsp_get_asr_block.hdr.err != 0) {
		clsic_info(clsic, "bulk response: %s\n",
			   clsic_error_string(
				msg_rsp->blkrsp_get_asr_block.hdr.err));
		asr_stream->error = true;
		snd_compr_fragment_elapsed(asr_stream->stream);
		return CLSIC_MSG_RELEASED;
	}

	payload_sz = msg_rsp->blkrsp_get_asr_block.hdr.bulk_sz;

	trace_clsic_vox_asr_stream_data_rcv_start(payload_sz);

	/* Extract data from the response to an intermediate buffer. */
	memcpy((uint8_t *) asr_stream->buf.data, msg->bulk_rxbuf, payload_sz);

	asr_stream->copied_total += payload_sz;

	/* Notify the compressed framework of available data. */
	snd_compr_fragment_elapsed(asr_stream->stream);

	trace_clsic_vox_asr_stream_data_rcv_end(payload_sz);

	return CLSIC_MSG_RELEASED;
}

/* Wait for initial keyphrase trigger from CLSIC. */
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

	ret = wait_for_completion_interruptible(&asr_stream->trigger_heard);
	if (ret || asr_stream->listen_error) {
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

	/* Fill in the trigger information. */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_GET_TRGR_INFO);
	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  (uint8_t *) &trgr_info,
				  sizeof(struct clsic_vox_trgr_info));
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		asr_stream->error = true;
		return 0;
	}

	/* Response is either bulk in case of success, or not. */
	if (!clsic_get_bulk_bit(msg_rsp.rsp_get_trgr_info.hdr.sbc))
		switch (msg_rsp.rsp_get_trgr_info.hdr.err) {
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_INPUT_PATH:
			clsic_err(vox->clsic, "failure %s.\n",
			 clsic_error_string(msg_rsp.rsp_get_trgr_info.hdr.err));
			asr_stream->error = true;
			return 0;
		default:
			clsic_err(vox->clsic, "unexpected CLSIC error code %d.\n",
				  msg_rsp.rsp_get_trgr_info.hdr.err);
			asr_stream->error = true;
			return 0;
		}

	if (trgr_info.engineid == VOX_TRGR_ENG_12_NUM)
		vox->trigger_engine_id = VOX_TRGR_ENG_12;
	else {
		clsic_err(vox->clsic, "unsupported trigger engine ID %d.\n",
			  trgr_info.engineid);
			asr_stream->error = true;
			return 0;
	}

	if ((trgr_info.phraseid == VOX_TRGR_PHR_1) ||
	    (trgr_info.phraseid == VOX_TRGR_PHR_2))
		/* 1 to 1 mapping of engine ID and enum index. */
		vox->trigger_phrase_id = trgr_info.phraseid;
	else {
		clsic_err(vox->clsic, "unsupported trigger engine ID %d.\n",
			  trgr_info.phraseid);
		asr_stream->error = true;
		return 0;
	}

	/* queue up the first read */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);

	reinit_completion(&asr_stream->asr_block_completion);
	asr_stream->asr_block_pending = true;
	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   (uint64_t) (uintptr_t) vox,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		clsic_err(clsic, "Error sending msg: %d\n", ret);
		/* force compressed fw to notice error */
		asr_stream->asr_block_pending = false;
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
		/* Fail if any ongoing vox operations. */
		mutex_lock(&vox->mgmt_mode_lock);
		if ((vox->mgmt_mode == VOX_MGMT_MODE_NEUTRAL) &&
		    (vox->asr_strm_mode == VOX_ASR_MODE_INACTIVE)) {
			vox->asr_strm_mode = VOX_ASR_MODE_STARTING;
			mutex_unlock(&vox->mgmt_mode_lock);
		} else {
			mutex_unlock(&vox->mgmt_mode_lock);
			return -EIO;
		}

		vox_update_barge_in(vox);

		ret = vox_set_mode(vox, CLSIC_VOX_MODE_LISTEN);
		if (ret) {
			ret = -EIO;
			goto exit;
		}

		reinit_completion(&asr_stream->trigger_heard);

		reinit_completion(&vox->new_bio_results_completion);

		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_LISTEN_START);

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
			ret = -EIO;
			goto exit;
		}
		if (msg_rsp.rsp_listen_start.hdr.err) {
			clsic_err(clsic,
				  "Failed to start listening: %s\n",
				  clsic_error_string(
					msg_rsp.rsp_listen_start.hdr.err));
			ret = -EIO;
			goto exit;
		}

		trace_clsic_vox_asr_stream_listen(
					  msg_cmd.cmd_listen_start.trgr_domain);

		vox->asr_stream.listen_error = false;
		asr_stream->asr_block_pending = false;

		asr_stream->wait_for_trigger =
			kthread_create(clsic_vox_asr_stream_wait_for_trigger,
				       asr_stream,
				       "clsic-vox-asr-wait-for-trigger");

		wake_up_process(asr_stream->wait_for_trigger);

		vox->asr_strm_mode = VOX_ASR_MODE_STREAMING;

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (asr_stream->asr_block_pending)
			/*
			 * Force a wait until the current block has completed
			 * before finishing up otherwise CLSIC complains.
			 */
			wait_for_completion(&asr_stream->asr_block_completion);

		vox->trigger_phrase_id = VOX_TRGR_INVALID;
		vox->trigger_engine_id = VOX_TRGR_INVALID;

		mutex_lock(&vox->mgmt_mode_lock);
		if ((vox->mgmt_mode == VOX_MGMT_MODE_NEUTRAL) &&
		    (vox->asr_strm_mode == VOX_ASR_MODE_STREAMING)) {
			vox->asr_strm_mode = VOX_ASR_MODE_STOPPING;
			mutex_unlock(&vox->mgmt_mode_lock);
		} else {
			mutex_unlock(&vox->mgmt_mode_lock);
			return -EIO;
		}

		ret = vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);
		if (ret)
			return -EIO;

		vox->asr_strm_mode = VOX_ASR_MODE_INACTIVE;

		break;
	default:
		return -EINVAL;
	}

exit:
	/* In case of failure during SNDRV_PCM_TRIGGER_START. */
	if (ret) {
		vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);

		vox->asr_strm_mode = VOX_ASR_MODE_INACTIVE;
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
	int ret = 0;

	if (asr_stream->error) {
		clsic_err(clsic, "ASR stream error.\n");
		return -EIO;
	}

	count = min(count, asr_stream->buf.size);

	trace_clsic_vox_asr_stream_copy_start(count);

	if (copy_to_user(buf, (uint8_t *) asr_stream->buf.data, count)) {
		clsic_err(clsic, "Failed to copy data to user.\n");
		return -EFAULT;
	}

	trace_clsic_vox_asr_stream_copy_end(count);

	/* queue up next read */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK);

	reinit_completion(&asr_stream->asr_block_completion);
	asr_stream->asr_block_pending = true;
	ret = clsic_send_msg_async(clsic,
				   (union t_clsic_generic_message *) &msg_cmd,
				   CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				   CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN,
				   (uint64_t) (uintptr_t) vox,
				   clsic_vox_asr_stream_data_cb);
	if (ret) {
		asr_stream->asr_block_pending = false;
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
	.open = clsic_vox_asr_stream_open,
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

	trace_clsic_vox_set_mode(new_mode);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	/*
	 * The VOX service handler should mark the secure processor as in use
	 * when it sets the device to modes other than IDLE and LISTEN
	 */
	if ((new_mode == CLSIC_VOX_MODE_IDLE) ||
	    (new_mode == CLSIC_VOX_MODE_LISTEN))
		clsic_msgproc_release(vox->clsic,
				      vox->service->service_instance);
	else
		clsic_msgproc_use(vox->clsic, vox->service->service_instance);

	switch (msg_rsp.rsp_set_mode.hdr.err) {
	case CLSIC_ERR_NONE:
		return 0;
	case CLSIC_ERR_INVAL_MODE_TRANSITION:
	case CLSIC_ERR_INVAL_MODE:
		clsic_err(vox->clsic, "%s.\n",
			  clsic_error_string(msg_rsp.rsp_set_mode.hdr.err));
		return -EIO;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_set_mode.hdr.err,
			  clsic_error_string(msg_rsp.rsp_set_mode.hdr.err));
		return -EIO;
	}
}

void vox_set_idle_and_mode(struct clsic_vox *vox, bool set_clsic_to_idle,
			   int mgmt_mode)
{
	if (set_clsic_to_idle)
		vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);

	vox->mgmt_mode = mgmt_mode;

	snd_ctl_notify(vox->codec->component.card->snd_card,
		       SNDRV_CTL_EVENT_MASK_VALUE, &vox->mgmt_mode_kctrl->id);
}

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

		ret = clsic_send_msg_sync(
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
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_INVAL_PHRASEID:
			clsic_err(vox->clsic, "failure %s.\n",
				  clsic_error_string(
				     msg_rsp.rsp_is_phrase_installed.hdr.err));
			return -EIO;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d.\n",
				  msg_rsp.rsp_is_phrase_installed.hdr.err);
			return -EIO;
		}
	}

	return 0;
}

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

		ret = clsic_send_msg_sync(
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
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_INVALID_BIN_ID:
		case CLSIC_ERR_INVALID_BIN_DATA:
			/* TODO: check these error codes are possible. */
			clsic_err(vox->clsic, "failure %s.\n",
				  clsic_error_string(
					msg_rsp.rsp_is_bin_installed.hdr.err));
			return -EIO;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d.\n",
				  msg_rsp.rsp_is_bin_installed.hdr.err);
			return -EIO;
		}
	}

	return 0;
}

static int vox_update_map(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	/* Map between VTE and biometric phrase. */
	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_IS_BIOVTE_MAP_INSTALLED);
	ret = clsic_send_msg_sync(vox->clsic,
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
	case CLSIC_ERR_BIOVTE_MAP_INVALID:
	case CLSIC_ERR_BIOVTE_MAP_SZ_INVALID:
	case CLSIC_ERR_BIOVTE_MAPPING_DOES_NOT_EXIST:
		/* TODO: check these error codes are possible. */
		clsic_err(vox->clsic, "failure %s.\n",
			  clsic_error_string(
				msg_rsp.rsp_is_biovte_map_installed.hdr.err));
		return -EIO;
	default:
		clsic_err(vox->clsic,
			  "unexpected CLSIC error code %d.\n",
			  msg_rsp.rsp_is_biovte_map_installed.hdr.err);
		return -EIO;
	}

	return 0;
}

static int vox_update_assets_status(struct clsic_vox *vox)
{
	struct clsic_service *handler =
		clsic_find_first_service(vox->clsic, CLSIC_SRV_TYPE_VOX);
	int ret;

	ret = vox_update_phrases(vox);
	if (ret)
		return ret;

	if (handler->service_version <= CLSIC_VOX_SRV_VERSION_MVP2)
		/* MVP2.0 nothing else to update. */
		return 0;

	ret = vox_update_bins(vox);
	if (ret)
		return ret;

	return vox_update_map(vox);
}

static int vox_update_user_status(struct clsic_vox *vox, uint8_t start_phr,
				  uint8_t end_phr)
{
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

			ret = clsic_send_msg_sync(
				     vox->clsic,
				     (union t_clsic_generic_message *) &msg_cmd,
				     (union t_clsic_generic_message *) &msg_rsp,
				     CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				     CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
			if (ret) {
				clsic_err(vox->clsic,
					  "clsic_send_msg_sync %d.\n", ret);
				return -EIO;
			}

			switch (msg_rsp.rsp_is_user_installed.hdr.err) {
			case CLSIC_ERR_NONE:
			    vox->user_installed[(phr * VOX_MAX_USERS) + usr] =
									true;
				break;
			case CLSIC_ERR_USER_NOT_INSTALLED:
			    vox->user_installed[(phr * VOX_MAX_USERS) + usr] =
									false;
				break;
			case CLSIC_ERR_INVAL_CMD_FOR_MODE:
			case CLSIC_ERR_INVAL_USERID:
			case CLSIC_ERR_INVAL_PHRASEID:
				clsic_err(vox->clsic, "failure %s.\n",
					  clsic_error_string(
				      msg_rsp.rsp_is_user_installed.hdr.err));
				return -EIO;
			default:
				clsic_err(vox->clsic,
					  "unexpected CLSIC error code %d.\n",
					 msg_rsp.rsp_is_user_installed.hdr.err);
				return -EIO;
			}
		}
	}

	return 0;
}

static int vox_update_bio_pub_key(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_GET_AUTH_KEY);

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  (uint8_t *) &vox->bio_pub_key,
				  sizeof(struct clsic_vox_auth_key));
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	/* Response is either bulk in case of success, or not. */
	if (clsic_get_bulk_bit(msg_rsp.rsp_get_auth_key.hdr.sbc))
		return 0;

	switch (msg_rsp.rsp_get_auth_key.hdr.err) {
	case CLSIC_ERR_INVAL_CMD_FOR_MODE:
	case CLSIC_ERR_KEY_NOT_FOUND:
		clsic_err(vox->clsic, "failure %s.\n",
			  clsic_error_string(msg_rsp.rsp_get_auth_key.hdr.err));
		return -EIO;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d.\n",
			  msg_rsp.rsp_get_auth_key.hdr.err);
		return -EIO;
	}
}

static int vox_install_asset(struct clsic_vox *vox)
{
	const struct firmware *fw;
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	char file[VOX_ASSET_TYPE_NAME_MAX_LEN];
	int id = -1;
	int ret;

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret) {
		clsic_err(vox->clsic, "%d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
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

	ret = request_firmware(&fw, file, vox->clsic->dev);
	if (ret) {
		clsic_err(vox->clsic, "request_firmware failed for %s.\n",
			  file);
		vox->error_info = VOX_ERROR_LIBRARY;
		goto exit;
	}

	if (fw->size % CLSIC_ASSET_SIZE_ALIGNMENT) {
		clsic_err(vox->clsic,
			  "firmware file %s size %d is not a multiple of %d.\n",
			  file, fw->size, CLSIC_ASSET_SIZE_ALIGNMENT);
		release_firmware(fw);
		vox->error_info = VOX_ERROR_LIBRARY;
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

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  fw->data, fw->size,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	release_firmware(fw);
	if (ret)
		goto exit;

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		switch (msg_rsp.rsp_install_phrase.hdr.err) {
		case CLSIC_ERR_NONE:
			/* Get updated information on enrolled users. */
			ret = vox_update_user_status(vox, vox->phrase_id,
						     vox->phrase_id);
			if (ret)
				goto exit;
			vox->phrase_installed[vox->phrase_id] = true;
			clsic_dbg(vox->clsic,
				  "successfully installed phrase %d.\n",
				  vox->phrase_id);
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_BPB_SZ_TOO_SMALL:
		case CLSIC_ERR_BPB_SZ_UNALIGNED:
		case CLSIC_ERR_BPB_BAD_HDR:
		case CLSIC_ERR_BPB_BAD_IMGMAP:
		case CLSIC_ERR_BPB_SZ_INCONSISTENT:
		case CLSIC_ERR_BPB_AUTH_FAILED:
		case CLSIC_ERR_BPB_ASSET_INVAL_VER:
		case CLSIC_ERR_BPB_ASSET_INVAL_SZ:
		case CLSIC_ERR_BPB_ASSET_INVAL_COMP_TYPE:
		case CLSIC_ERR_BPB_ASSET_INVAL_COMP_TABLE_SZ:
		case CLSIC_ERR_BPB_ASSET_INVAL_FLAGS:
			clsic_err(vox->clsic,
				  "phrase installation error %s.\n",
				  clsic_error_string(
					msg_rsp.rsp_install_phrase.hdr.err));
			vox->error_info = VOX_ERROR_BAD_ASSET;
			break;
		case CLSIC_ERR_NO_MEM:
		case CLSIC_ERR_FLASH:
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_INVAL_PHRASEID:
		case CLSIC_ERR_VOICEID:
			clsic_err(vox->clsic, "phrase installation error %s.\n",
				  clsic_error_string(
					msg_rsp.rsp_install_phrase.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_install_phrase.hdr.err,
				  clsic_error_string(
					msg_rsp.rsp_install_phrase.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
		}
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		/* TODO: check these are all the error codes possible. */
		switch (msg_rsp.rsp_install_bin.hdr.err) {
		case CLSIC_ERR_NONE:
			vox->bin_installed[vox->bin_id] = true;
			clsic_dbg(vox->clsic,
				  "successfully installed bin %d.\n",
				  vox->bin_id);
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_INVALID_BIN_DATA:
			clsic_err(vox->clsic,
				  "bin installation error %s.\n",
				  clsic_error_string(
					msg_rsp.rsp_install_bin.hdr.err));
			vox->error_info = VOX_ERROR_BAD_ASSET;
			break;
		case CLSIC_ERR_INVALID_BIN_ID:
		case CLSIC_ERR_NO_MEM:
		case CLSIC_ERR_FLASH:
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_VOICEID:
			clsic_err(vox->clsic, "bin installation error %s.\n",
				  clsic_error_string(
					msg_rsp.rsp_install_bin.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_install_bin.hdr.err,
				  clsic_error_string(
					msg_rsp.rsp_install_bin.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
		}
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		/* TODO: check these are all the error codes possible. */
		switch (msg_rsp.rsp_install_biovte_map.hdr.err) {
		case CLSIC_ERR_NONE:
			vox->bio_vte_map_installed = true;
			clsic_dbg(vox->clsic,
				  "successfully installed bin %d.\n",
				  vox->bin_id);
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_BIOVTE_MAP_SZ_INVALID:
		case CLSIC_ERR_BIOVTE_MAP_INVALID:
		case CLSIC_ERR_BIOVTE_MAPPING_DOES_NOT_EXIST:
			clsic_err(vox->clsic,
				  "biometric VTE installation error %s.\n",
				  clsic_error_string(
				       msg_rsp.rsp_install_biovte_map.hdr.err));
			vox->error_info = VOX_ERROR_BAD_ASSET;
			break;
		case CLSIC_ERR_NO_MEM:
		case CLSIC_ERR_FLASH:
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_VOICEID:
			clsic_err(vox->clsic,
				  "biometric VTE installation error %s.\n",
				  clsic_error_string(
				       msg_rsp.rsp_install_biovte_map.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_install_biovte_map.hdr.err,
				  clsic_error_string(
				       msg_rsp.rsp_install_biovte_map.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
		}
		break;
	}

exit:
	vox_set_idle_and_mode(vox, true, VOX_MGMT_MODE_NEUTRAL);

	return ret;
}

static int vox_uninstall_asset(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret, usr;

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret) {
		clsic_err(vox->clsic, "%d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
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
		trace_clsic_vox_uninstall_bio_vte_map(vox->phrase_id);
		clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
				   vox->service->service_instance,
				   CLSIC_VOX_MSG_CR_REMOVE_BIOVTE_MAP);
		break;
	}

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	}

	switch (vox->asset_type) {
	case VOX_ASSET_TYPE_PHRASE:
		switch (msg_rsp.rsp_remove_phrase.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_PHRASE_NOT_INSTALLED:
			clsic_dbg(vox->clsic,
				  "successfully uninstalled phrase %d.\n",
				  vox->phrase_id);
			/*
			 * Present no enrolled users for this phrase to reflect
			 * what CLSIC reports when there is no phrase installed.
			 */
			for (usr = CLSIC_VOX_USER1;
			     usr <= CLSIC_VOX_USER3;
			     usr++)
				vox->user_installed[
						(vox->phrase_id * VOX_MAX_USERS)
						+ usr] = false;
			vox->phrase_installed[vox->phrase_id] = false;
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_INVAL_PHRASEID:
		case CLSIC_ERR_VOICEID:
			clsic_err(vox->clsic, "%s.\n",
				  clsic_error_string(
					msg_rsp.rsp_remove_phrase.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_remove_phrase.hdr.err,
				  clsic_error_string(
					msg_rsp.rsp_remove_phrase.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		}
		break;
	case VOX_ASSET_TYPE_BIN_VTE:
	case VOX_ASSET_TYPE_BIN_SSF:
		/* TODO: check these are all the error codes possible. */
		switch (msg_rsp.rsp_remove_bin.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_BIN_NOT_INSTALLED:
			clsic_dbg(vox->clsic,
				  "successfully uninstalled bin %d.\n",
				  vox->bin_id);
			vox->bin_installed[vox->bin_id] = false;
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_INVALID_BIN_ID:
		case CLSIC_ERR_VOICEID:
			clsic_err(vox->clsic, "%s.\n",
				  clsic_error_string(
					msg_rsp.rsp_remove_bin.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_remove_bin.hdr.err,
				  clsic_error_string(
					msg_rsp.rsp_remove_bin.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		}
		break;
	case VOX_ASSET_TYPE_BIO_VTE_MAP:
		/* TODO: check these are all the error codes possible. */
		switch (msg_rsp.rsp_remove_biovte_map.hdr.err) {
		case CLSIC_ERR_NONE:
		case CLSIC_ERR_BIOVTE_MAP_NOT_INSTALLED:
			clsic_dbg(vox->clsic,
				  "successfully uninstalled biometric VTE map %d.\n",
				  vox->bin_id);
			vox->bio_vte_map_installed = false;
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_VOICEID:
			clsic_err(vox->clsic, "%s.\n",
				  clsic_error_string(
					msg_rsp.rsp_remove_biovte_map.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_remove_biovte_map.hdr.err,
				  clsic_error_string(
					msg_rsp.rsp_remove_biovte_map.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		}
		break;
	}

exit:
	vox_set_idle_and_mode(vox, true, VOX_MGMT_MODE_NEUTRAL);

	return ret;
}

static int vox_remove_user(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_remove_user(vox->user_id, vox->phrase_id);

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret) {
		clsic_err(vox->clsic, "%d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		goto exit;
	}

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_REMOVE_USER);
	msg_cmd.cmd_remove_user.phraseid = vox->phrase_id;
	msg_cmd.cmd_remove_user.userid = vox->user_id;

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	}

	switch (msg_rsp.rsp_remove_user.hdr.err) {
	case CLSIC_ERR_NONE:
	case CLSIC_ERR_USER_NOT_INSTALLED:
		vox->user_installed[(vox->phrase_id * VOX_MAX_USERS)
				    + vox->user_id] = false;
		vox->error_info = VOX_ERROR_SUCCESS;
		break;
	case CLSIC_ERR_INVAL_CMD_FOR_MODE:
	case CLSIC_ERR_INVAL_USERID:
	case CLSIC_ERR_INVAL_PHRASEID:
	case CLSIC_ERR_VOICEID:
		clsic_err(vox->clsic, "%s.\n",
			  clsic_error_string(msg_rsp.rsp_remove_user.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_remove_user.hdr.err,
			  clsic_error_string(msg_rsp.rsp_remove_user.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	}

exit:
	vox_set_idle_and_mode(vox, true, VOX_MGMT_MODE_NEUTRAL);

	return ret;
}

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
		clsic_err(vox->clsic, "%d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
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
			vox->error_info = VOX_ERROR_LIBRARY;
			goto exit;
		}

		msg_cmd.cmd_install_user_begin.phrase[0].phraseid =
								vox->phrase_id;
		msg_cmd.cmd_install_user_begin.phrase[0].rep_count =
							vox->number_of_reps;
	}

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	}

	switch (msg_rsp.rsp_install_user_begin.hdr.err) {
	case CLSIC_ERR_NONE:
		vox->error_info = VOX_ERROR_SUCCESS;
		break;
	case CLSIC_ERR_INVAL_CMD_FOR_MODE:
	case CLSIC_ERR_ALREADY_INSTALLING_USER:
	case CLSIC_ERR_INVAL_USERID:
	case CLSIC_ERR_INVAL_PHRASEID:
	case CLSIC_ERR_INVAL_REP_COUNT:
	case CLSIC_ERR_VOICEID:
	case CLSIC_ERR_INVALID_ENROL_DURATION:
	case CLSIC_ERR_PHRASE_NOT_INSTALLED:
		/* Could install the requisite phrase and try again? */
	case CLSIC_ERR_USER_ALREADY_INSTALLED:
		/* Could remove the user and try again? */
		clsic_err(vox->clsic, "%s.\n",
		    clsic_error_string(msg_rsp.rsp_install_user_begin.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_install_user_begin.hdr.err,
		    clsic_error_string(msg_rsp.rsp_install_user_begin.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	}

exit:
	if (ret)
		vox_set_idle_and_mode(vox, true, VOX_MGMT_MODE_NEUTRAL);
	else
		vox_set_idle_and_mode(vox, false, VOX_MGMT_MODE_STARTED_ENROL);

	return ret;
}

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

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	}

	switch (msg_rsp.rsp_rep_start.hdr.err) {
	case CLSIC_ERR_NONE:
	case CLSIC_ERR_ONGOING_REP:
		break;
	case CLSIC_ERR_REPS_COMPLETE:
	case CLSIC_ERR_INVAL_CMD_FOR_MODE:
	case CLSIC_ERR_NOT_INSTALLING_USER:
	case CLSIC_ERR_INPUT_PATH:
	case CLSIC_ERR_VOICEID:
		clsic_err(vox->clsic, "%s.\n",
			  clsic_error_string(msg_rsp.rsp_rep_start.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	case CLSIC_ERR_AUTH_NOT_STARTED_BARGE_IN:
		clsic_err(vox->clsic, "barge-in must be disabled.\n");
		vox->error_info = VOX_ERROR_DISABLE_BARGE_IN;
		ret = -EIO;
		break;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_rep_start.hdr.err,
			  clsic_error_string(msg_rsp.rsp_rep_start.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	}

exit:
	return ret;
}

static int vox_complete_enrolment(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	clsic_init_message((union t_clsic_generic_message *) &msg_cmd,
			   vox->service->service_instance,
			   CLSIC_VOX_MSG_CR_INSTALL_USER_COMPLETE);

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);

	trace_clsic_vox_complete_enrolment(0);

	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	}

	switch (msg_rsp.rsp_install_user_complete.hdr.err) {
	case CLSIC_ERR_NONE:
		vox->error_info = VOX_ERROR_SUCCESS;
		vox->user_installed[(vox->phrase_id * VOX_MAX_USERS)
				    + vox->user_id] = true;
		if ((vox->timeout > 0) && (vox->duration > 0))
			/* Implied combined enrolment. */
			vox->user_installed[
					(CLSIC_VOX_PHRASE_TI * VOX_MAX_USERS)
					+ vox->user_id] = true;
		break;
	case CLSIC_ERR_REPS_NOT_ENOUGH_VALID:
	case CLSIC_ERR_VOICEID:
	case CLSIC_ERR_FLASH:
		clsic_err(vox->clsic, "%s.\n",
		    clsic_error_string(
				msg_rsp.rsp_install_user_complete.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_install_user_complete.hdr.err,
			  clsic_error_string(
				msg_rsp.rsp_install_user_complete.hdr.err));
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		break;
	}

exit:
	vox_set_idle_and_mode(vox, true, VOX_MGMT_MODE_NEUTRAL);

	return ret;
}

static int vox_get_bio_results(struct clsic_vox *vox)
{
	union clsic_vox_msg msg_cmd;
	union clsic_vox_msg msg_rsp;
	int ret;

	trace_clsic_vox_get_bio_results(0);

	vox->get_bio_results_early_exit = false;
	memset(&vox->biometric_results, 0, sizeof(union bio_results_u));

	/* Firstly wait for CLSIC to notify us of new results. */
	wait_for_completion(&vox->new_bio_results_completion);
	reinit_completion(&vox->new_bio_results_completion);

	if (vox->get_bio_results_early_exit)
		/*
		 * We are here if the biometric results available notification
		 * never came (e.g. no detected users) and we decide to stop
		 * getting any more results.
		 */
		return -EBUSY;

	switch (vox->auth_error) {
	case CLSIC_ERR_NONE:
		break;
	case CLSIC_ERR_AUTH_NO_USERS_TO_MATCH:
		vox->error_info = VOX_ERROR_NO_USERS;
		ret = 0;
		goto exit;
	case CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED:
		/* The maximum amount of audio has been processed.*/
		vox->error_info = VOX_ERROR_BIO_TIME_EXCEEDED;
		ret = -EIO;
		goto exit;
	case CLSIC_ERR_PHRASE_NOT_INSTALLED:
		/* i.e. BPB not installed. */
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	case CLSIC_ERR_AUTH_NOT_STARTED_BARGE_IN:
	case CLSIC_ERR_AUTH_ABORT_BARGE_IN:
		vox->error_info = VOX_ERROR_DISABLE_BARGE_IN;
		ret = -EIO;
		goto exit;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  vox->auth_error, clsic_error_string(vox->auth_error));
		vox->error_info = VOX_ERROR_LIBRARY;
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

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  (uint8_t *) &vox->challenge,
				  sizeof(struct clsic_vox_auth_challenge),
				  (uint8_t *) &vox->biometric_results,
				  size_of_bio_results(vox->bio_results_format));
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		vox->error_info = VOX_ERROR_LIBRARY;
		ret = -EIO;
		goto exit;
	}

	/* Response is either bulk in case of success, or not. */
	if (clsic_get_bulk_bit(msg_rsp.rsp_auth_user.hdr.sbc)) {
		vox->error_info = VOX_ERROR_SUCCESS;
	} else {
		switch (msg_rsp.rsp_auth_user.hdr.err) {
		case CLSIC_ERR_NO_USER_IDENTIFIED:
		case CLSIC_ERR_AUTH_NO_USERS_TO_MATCH:
			vox->error_info = VOX_ERROR_NO_USERS;
			break;
		case CLSIC_ERR_AUTH_ABORT_BARGE_IN:
		case CLSIC_ERR_AUTH_NOT_STARTED_BARGE_IN:
			vox->error_info = VOX_ERROR_DISABLE_BARGE_IN;
			break;
		case CLSIC_ERR_INVAL_CMD_FOR_MODE:
		case CLSIC_ERR_CANCELLED:
		case CLSIC_ERR_TOO_SMALL:
		case CLSIC_ERR_INVAL_SECURITY_LVL:
		case CLSIC_ERR_PHRASE_NOT_INSTALLED:
		case CLSIC_ERR_VOICEID:
		case CLSIC_ERR_INPUT_PATH:
		case CLSIC_ERR_SECURITY_FAIL:
		case CLSIC_ERR_INVALID_AUTH_RESULT_FORMAT:
		case CLSIC_ERR_AUTH_BIOM_DISABLED:
			clsic_err(vox->clsic, "%s.\n",
				clsic_error_string(
				    msg_rsp.rsp_auth_user.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		default:
			clsic_err(vox->clsic,
				  "unexpected CLSIC error code %d: %s.\n",
				  msg_rsp.rsp_auth_user.hdr.err,
				  clsic_error_string(
					msg_rsp.rsp_auth_user.hdr.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			ret = -EIO;
			break;
		}
	}

exit:
	vox_set_idle_and_mode(vox, false, VOX_MGMT_MODE_NEUTRAL);

	return ret;
}

static void vox_stop_bio_results(struct clsic_vox *vox)
{
	vox->error_info = VOX_ERROR_SUCCESS;

	trace_clsic_vox_stop_bio_results(0);

	vox_set_idle_and_mode(vox, false, VOX_MGMT_MODE_NEUTRAL);
}

/*
 * Work function allows ALSA "get" control to return immediately while sending
 * multiple messages.
 */
static void vox_mgmt_mode_handler(struct work_struct *data)
{
	struct clsic_vox *vox = container_of(data, struct clsic_vox,
					     mgmt_mode_work);
	int ret;

	switch (vox->mgmt_mode) {
	case VOX_MGMT_MODE_INSTALLING_ASSET:
		ret = vox_install_asset(vox);
		if (ret)
			clsic_err(vox->clsic, "vox_install_asset ret %d.\n",
				  ret);
		break;
	case VOX_MGMT_MODE_UNINSTALLING_ASSET:
		ret = vox_uninstall_asset(vox);
		if (ret)
			clsic_err(vox->clsic, "vox_uninstall_asset ret %d.\n",
				  ret);
		break;
	case VOX_MGMT_MODE_REMOVING_USER:
		ret = vox_remove_user(vox);
		if (ret)
			clsic_err(vox->clsic, "vox_remove_user ret %d.\n", ret);
		break;
	case VOX_MGMT_MODE_STARTING_ENROL:
		ret = vox_start_enrol_user(vox);
		if (ret)
			clsic_err(vox->clsic, "vox_start_enrol_user ret %d.\n",
				  ret);
		break;
	case VOX_MGMT_MODE_PERFORMING_ENROL_REP:
		ret = vox_perform_enrol_rep(vox);
		if (ret)
			clsic_err(vox->clsic, "vox_perform_enrol_rep ret %d.\n",
				  ret);
		break;
	case VOX_MGMT_MODE_COMPLETING_ENROL:
		ret = vox_complete_enrolment(vox);
		if (ret)
			clsic_err(vox->clsic,
				  "vox_complete_enrolment ret %d.\n", ret);
		break;
	case VOX_MGMT_MODE_TERMINATING_ENROL:
		vox->error_info = VOX_ERROR_SUCCESS;
		vox_set_idle_and_mode(vox, true, VOX_MGMT_MODE_NEUTRAL);
		break;
	case VOX_MGMT_MODE_GETTING_BIO_RESULTS:
		ret = vox_get_bio_results(vox);
		if (ret)
			clsic_err(vox->clsic, "vox_get_bio_results ret %d.\n",
				  ret);
		break;
	case VOX_MGMT_MODE_STOPPING_BIO_RESULTS:
		vox_stop_bio_results(vox);
		break;
	default:
		clsic_err(vox->clsic, "unknown mode %d for scheduled work.\n",
			  vox->mgmt_mode);
	}
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

/* Handle getting of all INT kcontrols. */
static int vox_ctrl_int_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *e =
			(struct soc_mixer_control *) kcontrol->private_value;

	ucontrol->value.enumerated.item[0] = *(unsigned int *) e->dobj.private;

	return 0;
}

/* Handle setting of all INT kcontrols. */
static int vox_ctrl_int_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *e =
			(struct soc_mixer_control *) kcontrol->private_value;

	*(unsigned int *) e->dobj.private = ucontrol->value.enumerated.item[0];

	return 0;
}

/* Handle getting of all ENUM kcontrols. */
static int vox_ctrl_enum_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;

	ucontrol->value.enumerated.item[0] = *(unsigned int *) e->dobj.private;

	return 0;
}

/* Handle setting of all ENUM kcontrols. */
static int vox_ctrl_enum_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;

	*(unsigned int *) e->dobj.private = ucontrol->value.enumerated.item[0];

	return 0;
}

static int vox_ctrl_challenge(struct snd_kcontrol *kcontrol,
			      int op_flag,
			      unsigned int size,
			      unsigned int __user *tlv)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(be, struct clsic_vox, s_bytes_challenge);

	if (op_flag == SNDRV_CTL_TLV_OP_WRITE) {
		if (size != sizeof(struct clsic_vox_auth_challenge))
			return -EINVAL;

		if (copy_from_user(&vox->challenge, tlv,
				   sizeof(struct clsic_vox_auth_challenge)))
			return -EFAULT;
	} else {
		if (copy_to_user(tlv, &vox->challenge,
				 sizeof(struct clsic_vox_auth_challenge)))
			return -EFAULT;
	}

	return 0;
}

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

static int vox_ctrl_bio_pub_key(struct snd_kcontrol *kcontrol,
				int op_flag,
				unsigned int size,
				unsigned int __user *tlv)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(be, struct clsic_vox, s_bytes_bio_pub_key);

	if (op_flag == SNDRV_CTL_TLV_OP_WRITE)
		return -EACCES;
	if (copy_to_user(tlv, &vox->bio_pub_key,
			 sizeof(struct clsic_vox_auth_key)))
		return -EFAULT;

	return 0;
}

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

static int vox_ctrl_asset_installed_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int vox_ctrl_user_installed_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct clsic_vox *vox = (struct clsic_vox *) kcontrol->private_value;

	ucontrol->value.integer.value[0] =
	   vox->user_installed[(vox->phrase_id * VOX_MAX_USERS) + vox->user_id];

	return 0;
}

/*
 * This needs to be called when going into a management mode going into either
 * enrolment or trigger listening as these are the only 2 situations affected
 * by barge in.
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

	ret = clsic_send_msg_sync(vox->clsic,
				  (union t_clsic_generic_message *) &msg_cmd,
				  (union t_clsic_generic_message *) &msg_rsp,
				  CLSIC_NO_TXBUF, CLSIC_NO_TXBUF_LEN,
				  CLSIC_NO_RXBUF, CLSIC_NO_RXBUF_LEN);
	if (ret) {
		clsic_err(vox->clsic, "clsic_send_msg_sync %d.\n", ret);
		return -EIO;
	}

	/* rsp_barge_in_ena and rsp_barge_in_dis are identical. */
	switch (msg_rsp.rsp_barge_in_ena.hdr.err) {
	case CLSIC_ERR_NONE:
		return 0;
	default:
		clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
			  msg_rsp.rsp_barge_in_ena.hdr.err,
			  clsic_error_string(msg_rsp.rsp_barge_in_ena.hdr.err));
		return -EIO;
	}
}

static int vox_ctrl_barge_in_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e = (struct soc_enum *) kcontrol->private_value;
	struct clsic_vox *vox =
		container_of(e, struct clsic_vox, soc_enum_barge_in);

	vox->barge_in_status = ucontrol->value.enumerated.item[0];

	/* Only set barge-in now if CLSIC is already doing something. */
	if ((vox->mgmt_mode != VOX_MGMT_MODE_NEUTRAL) ||
	    (vox->asr_strm_mode != VOX_ASR_MODE_INACTIVE))
		return vox_update_barge_in(vox);

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

	mutex_lock(&vox->mgmt_mode_lock);
	if (vox->asr_strm_mode == VOX_ASR_MODE_STREAMING) {
		/* Streaming ASR data. */
		if (ucontrol->value.enumerated.item[0] ==
						VOX_MGMT_MODE_GET_BIO_RESULTS) {
			if (vox->mgmt_mode == VOX_MGMT_MODE_NEUTRAL) {
				vox->mgmt_mode =
					VOX_MGMT_MODE_GETTING_BIO_RESULTS;
				mutex_unlock(&vox->mgmt_mode_lock);
				schedule_work(&vox->mgmt_mode_work);
			} else {
				mutex_unlock(&vox->mgmt_mode_lock);
				ret = -EBUSY;
			}
		} else if (ucontrol->value.enumerated.item[0] ==
					VOX_MGMT_MODE_STOP_BIO_RESULTS) {
			/*
			 * Set CLSIC to IDLE mode in order to prevent CLSIC
			 * crashing due to bringing down the audio path while in
			 * CLSIC STREAM mode.
			 */
			if ((vox->mgmt_mode ==
					VOX_MGMT_MODE_GETTING_BIO_RESULTS) ||
			    (vox->mgmt_mode == VOX_MGMT_MODE_NEUTRAL)) {
				vox->mgmt_mode =
					VOX_MGMT_MODE_STOPPING_BIO_RESULTS;
				mutex_unlock(&vox->mgmt_mode_lock);
				/*
				 * Complete get_bio_results in case CLSIC is
				 * hung doing scheduled work while getting
				 * results from a previous action (waiting for
				 * CLSIC_VOX_MSG_N_NEW_AUTH_RESULT).
				 */
				vox->get_bio_results_early_exit = true;
				complete(&vox->new_bio_results_completion);
				schedule_work(&vox->mgmt_mode_work);
			} else {
				mutex_unlock(&vox->mgmt_mode_lock);
				ret = -EBUSY;
			}
		} else {
			mutex_unlock(&vox->mgmt_mode_lock);
			ret = -EINVAL;
		}
	} else if (vox->asr_strm_mode == VOX_ASR_MODE_INACTIVE) {
		/* Not streaming ASR data. */
		switch (ucontrol->value.enumerated.item[0]) {
		case VOX_MGMT_MODE_INSTALL_ASSET:
		case VOX_MGMT_MODE_UNINSTALL_ASSET:
		case VOX_MGMT_MODE_REMOVE_USER:
		case VOX_MGMT_MODE_START_ENROL:
			if (vox->mgmt_mode == VOX_MGMT_MODE_NEUTRAL) {
				/*
				 * Management mode goes from command
				 * e.g. INSTALL to a state e.g. INSTALLING
				 */
				vox->mgmt_mode =
					ucontrol->value.enumerated.item[0] + 1;
				mutex_unlock(&vox->mgmt_mode_lock);
				schedule_work(&vox->mgmt_mode_work);
			} else {
				mutex_unlock(&vox->mgmt_mode_lock);
				ret = -EBUSY;
			}
			break;
		case VOX_MGMT_MODE_PERFORM_ENROL_REP:
		case VOX_MGMT_MODE_COMPLETE_ENROL:
		case VOX_MGMT_MODE_TERMINATE_ENROL:
			if (vox->mgmt_mode == VOX_MGMT_MODE_STARTED_ENROL) {
				vox->mgmt_mode =
					ucontrol->value.enumerated.item[0] + 1;
				mutex_unlock(&vox->mgmt_mode_lock);
				schedule_work(&vox->mgmt_mode_work);
			} else {
				mutex_unlock(&vox->mgmt_mode_lock);
				ret = -EBUSY;
			}
			break;
		default:
			mutex_unlock(&vox->mgmt_mode_lock);
			ret = -EINVAL;
		}
	} else
		ret = -EINVAL;

	if (ret == -EINVAL)
		clsic_err(vox->codec,
			  "unable to switch to vox management mode %d with ASR stream mode %d.\n",
			  ucontrol->value.enumerated.item[0],
			  vox->asr_strm_mode);

	return ret;
}

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
		trace_clsic_vox_trigger_heard(false);

		/* Failed to trigger. */
		if (handler->service_version <= CLSIC_VOX_SRV_VERSION_MVP2)
			clsic_err(vox->clsic,
				"trigger detection error on CLSIC.\n");
		else
			clsic_err(vox->clsic,
				  "trigger detection error on CLSIC %d: %s.\n",
				  msg_nty->nty_listen_err.err,
				  clsic_error_string(
					msg_nty->nty_listen_err.err));

		vox->asr_stream.listen_error = true;

		if (vox->asr_stream.stream)
			complete(&vox->asr_stream.trigger_heard);

		break;
	case CLSIC_VOX_MSG_N_TRGR_DETECT:
		trace_clsic_vox_trigger_heard(true);

		/* Normal trigger. */
		vox->asr_stream.listen_error = false;

		if (vox->asr_stream.stream)
			complete(&vox->asr_stream.trigger_heard);

		break;
	case CLSIC_VOX_MSG_N_REP_COMPLETE:
		switch (msg_nty->nty_rep_complete.err) {
		case CLSIC_ERR_NONE:
			vox->error_info = VOX_ERROR_SUCCESS;
			break;
		case CLSIC_ERR_AUTH_ABORT_BARGE_IN:
			vox->error_info = VOX_ERROR_DISABLE_BARGE_IN;
			break;
		case CLSIC_ERR_REP_TRGR_TIMEOUT:
			vox->error_info = VOX_ERROR_TIMEOUT;
			break;
		case CLSIC_ERR_REP_NOISE_LVL:
		case CLSIC_ERR_REP_SNR:
			vox->error_info = VOX_ERROR_TOO_NOISY;
			break;
		case CLSIC_ERR_REP_SPEECH_RATIO:
		case CLSIC_ERR_REP_NET_SPEECH:
			vox->error_info = VOX_ERROR_MORE_SPEECH_NEEDED;
			break;
		case CLSIC_ERR_REP_SATURATION:
			vox->error_info = VOX_ERROR_TOO_LOUD;
			break;
		case CLSIC_ERR_INPUT_PATH:
		case CLSIC_ERR_VOICEID:
		case CLSIC_ERR_REP_UNEXPECTED_TRGR:
		case CLSIC_ERR_SECURITY_FAIL:
		case CLSIC_ERR_REP_FEATURE_OVERFLOW:
		case CLSIC_ERR_REP_PLOSIVE:
		case CLSIC_ERR_REP_REWIND_OVF:
			clsic_err(vox->clsic, "%s.\n",
				  clsic_error_string(
					msg_nty->nty_rep_complete.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			break;
		default:
			clsic_err(vox->clsic, "unexpected CLSIC error code %d: %s.\n",
				  msg_nty->nty_rep_complete.err,
				  clsic_error_string(
					msg_nty->nty_rep_complete.err));
			vox->error_info = VOX_ERROR_LIBRARY;
			break;
		}

		vox_set_idle_and_mode(vox, false, VOX_MGMT_MODE_STARTED_ENROL);

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

static int clsic_vox_codec_probe(struct snd_soc_codec *codec)
{
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(codec);
	struct clsic_service *handler = vox->service;
	int ret;

	dev_info(codec->dev, "%s() %p.\n", __func__, codec);

	vox->codec = codec;

	init_completion(&vox->asr_stream.trigger_heard);

	vox->mgmt_mode = VOX_MGMT_MODE_NEUTRAL;

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);
	if (ret != 0)
		return ret;

	vox->asr_strm_mode = VOX_ASR_MODE_INACTIVE;

	mutex_init(&vox->mgmt_mode_lock);

	INIT_WORK(&vox->mgmt_mode_work, vox_mgmt_mode_handler);

	vox->kcontrol_new[0].name = "Vox Management Mode";
	vox->kcontrol_new[0].info = snd_soc_info_enum_double;
	vox->kcontrol_new[0].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[0].get = vox_ctrl_enum_get;
	vox->kcontrol_new[0].put = vox_ctrl_mgmt_put;
	vox->soc_enum_mode.items = VOX_NUM_MGMT_MODES;
	vox->soc_enum_mode.texts = vox_mgmt_mode_text;
	vox->soc_enum_mode.dobj.private = &vox->mgmt_mode;
	vox->kcontrol_new[0].private_value =
					(unsigned long) &vox->soc_enum_mode;
	vox->kcontrol_new[0].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->error_info = VOX_ERROR_CLEARED;

	vox->kcontrol_new[1].name = "Vox Error Info";
	vox->kcontrol_new[1].info = snd_soc_info_enum_double;
	vox->kcontrol_new[1].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[1].get = vox_ctrl_enum_get;
	vox->kcontrol_new[1].put = vox_ctrl_error_info_put;
	vox->soc_enum_error_info.items = VOX_NUM_ERRORS;
	vox->soc_enum_error_info.texts = vox_error_info_text;
	vox->soc_enum_error_info.dobj.private = &vox->error_info;
	vox->kcontrol_new[1].private_value =
				(unsigned long)(&(vox->soc_enum_error_info));
	vox->kcontrol_new[1].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->phrase_id = CLSIC_VOX_PHRASE_VDT1;

	memset(&vox->phrase_id_mixer_ctrl, 0,
	       sizeof(vox->phrase_id_mixer_ctrl));
	vox->phrase_id_mixer_ctrl.min = 0;
	vox->phrase_id_mixer_ctrl.max = VOX_MAX_PHRASES - 1;
	vox->phrase_id_mixer_ctrl.platform_max = VOX_MAX_PHRASES - 1;
	vox->phrase_id_mixer_ctrl.dobj.private = &vox->phrase_id;
	vox->kcontrol_new[2].name = "Vox Phrase ID";
	vox->kcontrol_new[2].info = snd_soc_info_volsw;
	vox->kcontrol_new[2].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[2].get = vox_ctrl_int_get;
	vox->kcontrol_new[2].put = vox_ctrl_int_put;
	vox->kcontrol_new[2].private_value =
		(unsigned long)(&(vox->phrase_id_mixer_ctrl));
	vox->kcontrol_new[2].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_MANAGE);
	if (ret != 0)
		return ret;

	ret = vox_update_assets_status(vox);
	if (ret != 0)
		return ret;

	vox->kcontrol_new[3].name = "Vox Asset Installed";
	vox->kcontrol_new[3].info = snd_soc_info_bool_ext;
	vox->kcontrol_new[3].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[3].get = vox_ctrl_asset_installed_get;
	vox->kcontrol_new[3].put = vox_ctrl_asset_installed_put;
	vox->kcontrol_new[3].private_value = (unsigned long)vox;
	vox->kcontrol_new[3].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->user_id = CLSIC_VOX_USER1;

	memset(&vox->user_id_mixer_ctrl, 0, sizeof(vox->user_id_mixer_ctrl));
	vox->user_id_mixer_ctrl.min = CLSIC_VOX_USER1;
	vox->user_id_mixer_ctrl.max = CLSIC_VOX_USER3;
	vox->user_id_mixer_ctrl.platform_max = CLSIC_VOX_USER3;
	vox->user_id_mixer_ctrl.dobj.private = &vox->user_id;
	vox->kcontrol_new[4].name = "Vox User ID";
	vox->kcontrol_new[4].info = snd_soc_info_volsw;
	vox->kcontrol_new[4].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[4].get = vox_ctrl_int_get;
	vox->kcontrol_new[4].put = vox_ctrl_int_put;
	vox->kcontrol_new[4].private_value =
		(unsigned long)(&(vox->user_id_mixer_ctrl));
	vox->kcontrol_new[4].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ret = vox_update_user_status(vox, CLSIC_VOX_PHRASE_VDT1,
				     CLSIC_VOX_PHRASE_TI);
	if (ret != 0)
		return ret;

	vox->kcontrol_new[5].name = "Vox User Installed";
	vox->kcontrol_new[5].info = snd_soc_info_bool_ext;
	vox->kcontrol_new[5].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[5].get = vox_ctrl_user_installed_get;
	vox->kcontrol_new[5].private_value = (unsigned long)vox;
	vox->kcontrol_new[5].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->duration = VOX_DEFAULT_DURATION;

	memset(&vox->duration_mixer_ctrl, 0, sizeof(vox->duration_mixer_ctrl));
	vox->duration_mixer_ctrl.min = 0;
	vox->duration_mixer_ctrl.max = VOX_MAX_DURATION_TIMEOUT;
	vox->duration_mixer_ctrl.platform_max = VOX_MAX_DURATION_TIMEOUT;
	vox->duration_mixer_ctrl.dobj.private = &vox->duration;
	vox->kcontrol_new[6].name = "Vox Duration in ms";
	vox->kcontrol_new[6].info = snd_soc_info_volsw;
	vox->kcontrol_new[6].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[6].get = vox_ctrl_int_get;
	vox->kcontrol_new[6].put = vox_ctrl_int_put;
	vox->kcontrol_new[6].private_value =
		(unsigned long)(&(vox->duration_mixer_ctrl));
	vox->kcontrol_new[6].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->timeout = VOX_DEFAULT_TIMEOUT;

	memset(&vox->timeout_mixer_ctrl, 0, sizeof(vox->timeout_mixer_ctrl));
	vox->timeout_mixer_ctrl.min = 0;
	vox->timeout_mixer_ctrl.max = VOX_MAX_DURATION_TIMEOUT;
	vox->timeout_mixer_ctrl.platform_max = VOX_MAX_DURATION_TIMEOUT;
	vox->timeout_mixer_ctrl.dobj.private = &vox->timeout;
	vox->kcontrol_new[7].name = "Vox Timeout in ms";
	vox->kcontrol_new[7].info = snd_soc_info_volsw;
	vox->kcontrol_new[7].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[7].get = vox_ctrl_int_get;
	vox->kcontrol_new[7].put = vox_ctrl_int_put;
	vox->kcontrol_new[7].private_value =
		(unsigned long)(&(vox->timeout_mixer_ctrl));
	vox->kcontrol_new[7].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->number_of_reps = VOX_DEFAULT_NUM_REPS;

	memset(&vox->reps_mixer_ctrl, 0, sizeof(vox->reps_mixer_ctrl));
	vox->reps_mixer_ctrl.min = 1;
	vox->reps_mixer_ctrl.max = VOX_MAX_NUM_REPS;
	vox->reps_mixer_ctrl.platform_max = VOX_MAX_NUM_REPS;
	vox->reps_mixer_ctrl.dobj.private = &vox->number_of_reps;
	vox->kcontrol_new[8].name = "Vox Number of Enrolment Repetitions";
	vox->kcontrol_new[8].info = snd_soc_info_volsw;
	vox->kcontrol_new[8].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[8].get = vox_ctrl_int_get;
	vox->kcontrol_new[8].put = vox_ctrl_int_put;
	vox->kcontrol_new[8].private_value =
		(unsigned long)(&(vox->reps_mixer_ctrl));
	vox->kcontrol_new[8].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->security_level = VOX_SEC_LEVEL_LOW;

	vox->kcontrol_new[9].name = "Vox Security Level";
	vox->kcontrol_new[9].info = snd_soc_info_enum_double;
	vox->kcontrol_new[9].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[9].get = vox_ctrl_enum_get;
	vox->kcontrol_new[9].put = vox_ctrl_enum_put;
	vox->soc_enum_sec_level.items = VOX_NUM_SEC_LEVEL;
	vox->soc_enum_sec_level.texts = vox_sec_level_text;
	vox->soc_enum_sec_level.dobj.private = &vox->security_level;
	vox->kcontrol_new[9].private_value =
				(unsigned long)(&(vox->soc_enum_sec_level));
	vox->kcontrol_new[9].access = SNDRV_CTL_ELEM_ACCESS_READ |
				      SNDRV_CTL_ELEM_ACCESS_WRITE |
				      SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->bio_results_format = VOX_BIO_RESULTS_CLASSIC;

	vox->kcontrol_new[10].name = "Vox Biometric Results Format";
	vox->kcontrol_new[10].info = snd_soc_info_enum_double;
	vox->kcontrol_new[10].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[10].get = vox_ctrl_enum_get;
	vox->kcontrol_new[10].put = vox_ctrl_enum_put;
	vox->soc_enum_bio_res_type.items = VOX_NUM_BIO_RESULTS_FORMATS;
	vox->soc_enum_bio_res_type.texts = vox_bio_results_format_text;
	vox->soc_enum_bio_res_type.dobj.private = &vox->bio_results_format;
	vox->kcontrol_new[10].private_value =
				(unsigned long)(&(vox->soc_enum_bio_res_type));
	vox->kcontrol_new[10].access = SNDRV_CTL_ELEM_ACCESS_READ |
				       SNDRV_CTL_ELEM_ACCESS_WRITE |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	memset(&vox->challenge, 0, sizeof(struct clsic_vox_auth_challenge));

	vox->s_bytes_challenge.max = sizeof(struct clsic_vox_auth_challenge);
	vox->kcontrol_new[11].name = "Vox Challenge";
	vox->kcontrol_new[11].info = snd_soc_bytes_info_ext;
	vox->kcontrol_new[11].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[11].tlv.c = vox_ctrl_challenge;
	vox->kcontrol_new[11].private_value =
				(unsigned long)(&(vox->s_bytes_challenge));
	vox->kcontrol_new[11].access = SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE |
				       SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	memset(&vox->biometric_results, 0, sizeof(union bio_results_u));

	vox->s_bytes_bio_res.max = sizeof(union bio_results_u);
	vox->kcontrol_new[12].name = "Vox Signed Biometric Results Blob";
	vox->kcontrol_new[12].info = snd_soc_bytes_info_ext;
	vox->kcontrol_new[12].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[12].tlv.c = vox_ctrl_bio_res_blob;
	vox->kcontrol_new[12].private_value =
				(unsigned long)(&(vox->s_bytes_bio_res));
	vox->kcontrol_new[12].access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				       SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	memset(&vox->bio_pub_key, 0, sizeof(struct clsic_vox_auth_key));
	ret = vox_update_bio_pub_key(vox);
	if (ret != 0)
		return ret;

	vox->s_bytes_bio_pub_key.max = sizeof(struct clsic_vox_auth_key);
	vox->kcontrol_new[13].name = "Vox Biometric Result Public Key";
	vox->kcontrol_new[13].info = snd_soc_bytes_info_ext;
	vox->kcontrol_new[13].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[13].tlv.c = vox_ctrl_bio_pub_key;
	vox->kcontrol_new[13].private_value =
				(unsigned long)(&(vox->s_bytes_bio_pub_key));
	vox->kcontrol_new[13].access = SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				       SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->barge_in_status = VOX_BARGE_IN_DISABLED;

	vox->kcontrol_new[14].name = "Vox Barge-In";
	vox->kcontrol_new[14].info = snd_soc_info_enum_double;
	vox->kcontrol_new[14].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[14].get = vox_ctrl_enum_get;
	vox->kcontrol_new[14].put = vox_ctrl_barge_in_put;
	vox->soc_enum_barge_in.items = VOX_NUM_BARGE_IN;
	vox->soc_enum_barge_in.texts = vox_barge_in_text;
	vox->soc_enum_barge_in.dobj.private = &vox->barge_in_status;
	vox->kcontrol_new[14].private_value =
				(unsigned long)(&(vox->soc_enum_barge_in));

	vox->bin_id = CLSIC_VOX_BIN_VTE1;

	memset(&vox->bin_id_mixer_ctrl, 0, sizeof(vox->bin_id_mixer_ctrl));
	vox->bin_id_mixer_ctrl.min = 0;
	vox->bin_id_mixer_ctrl.max = CLSIC_VOX_BIN_CNT - 1;
	vox->bin_id_mixer_ctrl.platform_max = CLSIC_VOX_BIN_CNT - 1;
	vox->bin_id_mixer_ctrl.dobj.private = &vox->bin_id;
	vox->kcontrol_new[15].name = "Vox Bin ID";
	vox->kcontrol_new[15].info = snd_soc_info_volsw;
	vox->kcontrol_new[15].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[15].get = vox_ctrl_int_get;
	vox->kcontrol_new[15].put = vox_ctrl_int_put;
	vox->kcontrol_new[15].private_value =
		(unsigned long)(&(vox->bin_id_mixer_ctrl));
	vox->kcontrol_new[15].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->asset_type = VOX_ASSET_TYPE_PHRASE;

	vox->kcontrol_new[16].name = "Vox Asset Type";
	vox->kcontrol_new[16].info = snd_soc_info_enum_double;
	vox->kcontrol_new[16].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[16].get = vox_ctrl_enum_get;
	vox->kcontrol_new[16].put = vox_ctrl_enum_put;
	if (handler->service_version <= CLSIC_VOX_SRV_VERSION_MVP2)
		vox->soc_enum_asset_type.items = VOX_NUM_ASSET_TYPES_MVP2;
	else
		vox->soc_enum_asset_type.items = VOX_NUM_ASSET_TYPES_MVP;
	vox->soc_enum_asset_type.texts = vox_asset_type_text_mvp;

	vox->soc_enum_asset_type.dobj.private = &vox->asset_type;
	vox->kcontrol_new[16].private_value =
				(unsigned long) &vox->soc_enum_asset_type;
	vox->kcontrol_new[16].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->file_id = 0;

	memset(&vox->file_id_mixer_ctrl, 0, sizeof(vox->file_id_mixer_ctrl));
	vox->file_id_mixer_ctrl.min = 0;
	vox->file_id_mixer_ctrl.max = UINT_MAX;
	vox->file_id_mixer_ctrl.platform_max = UINT_MAX;
	vox->file_id_mixer_ctrl.dobj.private = &vox->file_id;
	vox->kcontrol_new[17].name = "Vox Asset Filename ID";
	vox->kcontrol_new[17].info = snd_soc_info_volsw;
	vox->kcontrol_new[17].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[17].get = vox_ctrl_int_get;
	vox->kcontrol_new[17].put = vox_ctrl_int_put;
	vox->kcontrol_new[17].private_value =
		(unsigned long)(&(vox->file_id_mixer_ctrl));
	vox->kcontrol_new[17].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->trigger_phrase_id = VOX_TRGR_INVALID;

	vox->kcontrol_new[18].name = "Vox Trigger Phrase ID";
	vox->kcontrol_new[18].info = snd_soc_info_enum_double;
	vox->kcontrol_new[18].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[18].get = vox_ctrl_enum_get;
	vox->kcontrol_new[18].put = vox_ctrl_enum_put;
	vox->soc_enum_trgr_phr.items = VOX_NUM_TRGR_PHR;
	vox->soc_enum_trgr_phr.texts = vox_trgr_phr_text;
	vox->soc_enum_trgr_phr.dobj.private = &vox->trigger_phrase_id;
	vox->kcontrol_new[18].private_value =
				(unsigned long)(&(vox->soc_enum_trgr_phr));
	vox->kcontrol_new[18].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	vox->trigger_engine_id = VOX_TRGR_INVALID;

	vox->kcontrol_new[19].name = "Vox Trigger Engine ID";
	vox->kcontrol_new[19].info = snd_soc_info_enum_double;
	vox->kcontrol_new[19].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	vox->kcontrol_new[19].get = vox_ctrl_enum_get;
	vox->kcontrol_new[19].put = vox_ctrl_enum_put;
	vox->soc_enum_trgr_eng.items = VOX_NUM_TRGR_ENG;
	vox->soc_enum_trgr_eng.texts = vox_trgr_eng_text;
	vox->soc_enum_trgr_eng.dobj.private = &vox->trigger_engine_id;
	vox->kcontrol_new[19].private_value =
				(unsigned long)(&(vox->soc_enum_trgr_eng));
	vox->kcontrol_new[19].access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				       SNDRV_CTL_ELEM_ACCESS_VOLATILE;

	ret = snd_soc_add_codec_controls(codec, vox->kcontrol_new,
					 VOX_NUM_NEW_KCONTROLS);
	if (ret != 0) {
		pr_err("enum %s() add ret: %d.\n", __func__, ret);
		return ret;
	}

	vox->get_bio_results_early_exit = false;
	init_completion(&vox->new_bio_results_completion);

	init_completion(&vox->asr_stream.asr_block_completion);

	ret = vox_set_mode(vox, CLSIC_VOX_MODE_IDLE);
	if (ret != 0)
		return ret;

	vox->mgmt_mode_kctrl = snd_soc_card_get_kcontrol(
						vox->codec->component.card,
						"Vox Management Mode");

	handler->data = (void *) vox;
	handler->callback = &vox_notification_handler;

	return ret;
}

static int clsic_vox_codec_remove(struct snd_soc_codec *codec)
{
	struct clsic_vox *vox = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s() %p %p.\n", __func__, codec, vox);

	cancel_work_sync(&vox->mgmt_mode_work);

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

	vox = devm_kzalloc(&pdev->dev, sizeof(struct clsic_vox), GFP_KERNEL);
	if (vox == NULL)
		return -ENOMEM;

	vox->clsic = clsic;
	vox->service = clsic->service_handlers[vox_service->service_instance];

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

MODULE_DESCRIPTION("ASoC Cirrus Logic CLSIC vox codec");
MODULE_AUTHOR("Piotr Stankiewicz <piotrs@opensource.wolfsonmicro.com>");
MODULE_AUTHOR("Ralph Clark <ralph.clark@cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clsic-vox");
