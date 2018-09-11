/*
 * clsic-vox.h -- ALSA SoC CLSIC VOX header
 *
 * Copyright (C) 2015-2018 Cirrus Logic, Inc. and
 *			   Cirrus Logic International Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define VOX_ASR_MIN_FRAGMENT_SZ	0
#define VOX_ASR_MAX_FRAGMENT_SZ	307200
#define VOX_ASR_MIN_FRAGMENTS	1
#define VOX_ASR_MAX_FRAGMENTS	256

#define VOX_MAX_USERS		3
#define VOX_MAX_PHRASES		5

#define VOX_NUM_NEW_KCONTROLS	27

#define CLSIC_ASSET_SIZE_ALIGNMENT	4

#define VOX_DEFAULT_DURATION		0
#define VOX_DEFAULT_TIMEOUT		4000
#define VOX_MAX_DURATION_TIMEOUT	0xFFFF
#define VOX_DEFAULT_NUM_REPS		3
#define VOX_MAX_NUM_REPS		5

#define CLSIC_VOX_SRV_VERSION_MVP2	0x00030002	/* 2.0.248 */

/**
 * struct clsic_asr_stream_buf - audio buffer descriptor for use in ASR
 *				streaming operations
 * @data:	Shared buffer for ping-ponging data.
 * @size:	Size of the buffer.
 * @frag_sz:	Fragment size.
 *
 * Information about the intermediate buffer used for copying ASR data from
 * CLSIC to userspace.
 */
struct clsic_asr_stream_buf {
	void *data;
	size_t size;
	size_t frag_sz;
};

/**
 * struct clsic_asr_stream - struct for managing all ASR operations
 * @buf:	Shared buffer for ping-ponging data.
 * @stream:	From the compressed stream infrastructure.
 * @block_sz:	Size of ASR block to obtain from CLSIC per message. A size of 0
 *		means we do not intend to stream data.
 * @copied_total:	Total ASR data copied in this stream.
 * @sample_rate:	Sample rate in Hz of this stream.
 * @error:		If there is a problem in setting up the ASR stream
 *			before or after trigger has occurred, this is set to
 *			prevent further attempts to stream data.
 * @wait_for_trigger:	Thread to wait for the initial trigger.
 * @completion:		Completion that is used in waiting for a trigger to
 *			occur.
 *
 * Information about the intermediate buffer used for copying ASR data from
 * CLSIC to userspace.
 */
struct clsic_asr_stream {
	struct clsic_asr_stream_buf buf;

	struct snd_compr_stream *stream;

	unsigned int block_sz;
	unsigned int copied_total;

	unsigned int sample_rate;

	struct task_struct *wait_for_trigger;
	struct completion completion;
	struct mutex stream_lock;

	bool cb_error;
	bool listen_error;
};

/**
 * union bio_results_u - union for containing the maximum size of biometric
 *			results blob from CLSIC
 * @result:	Results of biometric authentication in "classic" format.
 * @result_ex:	Results of biometric authentication in "extended" format.
 * @result_ex2:	Results of biometric authentication in "extended2" format.
 *
 * This is used as a safe maximum size container for managing biometric results.
 */
union bio_results_u {
	struct clsic_vox_auth_result result;
	struct clsic_vox_auth_result_ex result_ex;
	struct clsic_vox_auth_result_ex2 result_ex2;
};

/**
 * struct clsic_vox - the major struct used within the vox driver
 *
 * This struct is used everywhere within the vox driver to handle operations
 * shared between functions.
 */
struct clsic_vox {
	struct clsic *clsic;
	struct clsic_service *service;
	struct snd_soc_codec *codec;

	/* ASR data stream */
	struct clsic_asr_stream asr_stream;

	struct snd_kcontrol_new kcontrol_new[VOX_NUM_NEW_KCONTROLS];
	struct mutex drv_state_lock;
	/* drv_state refers to ongoing vox biometric operations only. */
	unsigned int drv_state;
	/* Which mode CLSIC is in. */
	enum clsic_vox_mode clsic_mode;
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
	unsigned int clsic_error_code;
	uint32_t scc_status;
	int32_t scc_cap_delay_ms;
	int32_t scc_triggerpoint;
	int32_t scc_cap_preamble_ms;
	struct clsic_vox_auth_challenge challenge;
	union bio_results_u biometric_results;
	struct clsic_vox_auth_key bio_pub_key;
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

	struct soc_mixer_control phrase_id_mixer_ctrl;
	struct soc_mixer_control user_id_mixer_ctrl;
	struct soc_mixer_control duration_mixer_ctrl;
	struct soc_mixer_control timeout_mixer_ctrl;
	struct soc_mixer_control reps_mixer_ctrl;
	struct soc_mixer_control bin_id_mixer_ctrl;
	struct soc_mixer_control file_id_mixer_ctrl;
	struct soc_mixer_control clsic_error_mixer_ctrl;
	struct soc_mixer_control trgr_phrase_id_mixer_ctrl;
	struct soc_mixer_control trgr_engine_id_mixer_ctrl;

	struct soc_bytes_ext s_bytes_challenge;
	struct soc_bytes_ext s_bytes_bio_res;
	struct soc_bytes_ext s_bytes_bio_pub_key;
	struct soc_bytes_ext s_bytes_scc_manage_ack;
	struct soc_bytes_ext s_bytes_scc_status;
	struct soc_bytes_ext s_bytes_scc_cap_delay_ms;
	struct soc_bytes_ext s_bytes_scc_triggerpoint;
	struct soc_bytes_ext s_bytes_scc_cap_preamble_ms;
	struct soc_bytes_ext s_bytes_scc_phraseid;

	bool phrase_installed[VOX_MAX_PHRASES];
	bool user_installed[VOX_MAX_PHRASES * VOX_MAX_USERS];
	bool bin_installed[CLSIC_VOX_BIN_CNT];
	bool bio_vte_map_installed;

	struct work_struct drv_state_work;
	struct snd_kcontrol *error_info_kctrl;

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

#define VOX_NUM_DRV_STATES			22

#define VOX_DRV_STATE_NEUTRAL			0
#define VOX_DRV_STATE_INSTALL_ASSET		1
#define VOX_DRV_STATE_INSTALLING_ASSET		2
#define VOX_DRV_STATE_UNINSTALL_ASSET		3
#define VOX_DRV_STATE_UNINSTALLING_ASSET	4
#define VOX_DRV_STATE_REMOVE_USER		5
#define VOX_DRV_STATE_REMOVING_USER		6
#define VOX_DRV_STATE_START_ENROL		7
#define VOX_DRV_STATE_STARTING_ENROL		8
#define VOX_DRV_STATE_ENROLLING			9
#define VOX_DRV_STATE_PERFORM_ENROL_REP		10
#define VOX_DRV_STATE_PERFORMING_ENROL_REP	11
#define VOX_DRV_STATE_COMPLETE_ENROL		12
#define VOX_DRV_STATE_COMPLETING_ENROL		13
#define VOX_DRV_STATE_TERMINATE_ENROL		14
#define VOX_DRV_STATE_TERMINATING_ENROL		15
#define VOX_DRV_STATE_GET_BIO_RESULTS		16
#define VOX_DRV_STATE_GETTING_BIO_RESULTS	17
#define VOX_DRV_STATE_STOP_BIO_RESULTS		18
#define VOX_DRV_STATE_STOPPING_BIO_RESULTS	19
#define VOX_DRV_STATE_LISTENING			20
#define VOX_DRV_STATE_STREAMING			21

static const char *vox_drv_state_text[VOX_NUM_DRV_STATES] = {
	[VOX_DRV_STATE_NEUTRAL]			= "Neutral",
	[VOX_DRV_STATE_INSTALL_ASSET]		= "Install Asset",
	[VOX_DRV_STATE_INSTALLING_ASSET]	= "Installing Asset",
	[VOX_DRV_STATE_UNINSTALL_ASSET]		= "Uninstall Asset",
	[VOX_DRV_STATE_UNINSTALLING_ASSET]	= "Uninstalling Asset",
	[VOX_DRV_STATE_REMOVE_USER]		= "Remove User",
	[VOX_DRV_STATE_REMOVING_USER]		= "Removing User",
	[VOX_DRV_STATE_START_ENROL]		= "Start User Enrolment",
	[VOX_DRV_STATE_STARTING_ENROL]		= "Starting User Enrolment",
	[VOX_DRV_STATE_ENROLLING]		= "Enrolling User",
	[VOX_DRV_STATE_PERFORM_ENROL_REP] = "Perform Enrolment Repetition",
	[VOX_DRV_STATE_PERFORMING_ENROL_REP] =
					    "Performing Enrolment Repetition",
	[VOX_DRV_STATE_COMPLETE_ENROL]		= "Complete User Enrolment",
	[VOX_DRV_STATE_COMPLETING_ENROL]	= "Completing User Enrolment",
	[VOX_DRV_STATE_TERMINATE_ENROL]		= "Terminate User Enrolment",
	[VOX_DRV_STATE_TERMINATING_ENROL]	= "Terminating User Enrolment",
	[VOX_DRV_STATE_GET_BIO_RESULTS]		= "Get Biometric Results",
	[VOX_DRV_STATE_GETTING_BIO_RESULTS]	= "Getting Biometric Results",
	[VOX_DRV_STATE_STOP_BIO_RESULTS]	= "Stop Biometric Results",
	[VOX_DRV_STATE_STOPPING_BIO_RESULTS]	= "Stopping Biometric Results",
	[VOX_DRV_STATE_LISTENING]		= "Listening For Trigger",
	[VOX_DRV_STATE_STREAMING]		= "Streaming ASR Data",
};

#define VOX_NUM_ERRORS			4

#define VOX_ERROR_SUCCESS		0
#define VOX_ERROR_DRIVER		1
#define VOX_ERROR_CLSIC			2
#define VOX_ERROR_CLEARED		3

static const char *vox_error_info_text[VOX_NUM_ERRORS] = {
	[VOX_ERROR_SUCCESS]		= "Success",
	[VOX_ERROR_DRIVER]		= "Driver Error",
	[VOX_ERROR_CLSIC]		= "CLSIC Error",
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

/*
 * Valid values for Engine and Phrase IDs are 0 to 255 (8 bit unsigned
 * integer), the vox service stores the value in a signed 32 bit integer and an
 * INT ALSA control so the value of -1 can be stored and communicated safely
 * through the stack.
 */
#define VOX_TRGR_INVALID		-1

/**
 * set_error_info() - set the error info control value for userspace
 * @vox:	The main instance of struct clsic_vox used in this driver.
 * @ret:	The return code from another function.
 *
 * This function sets the internal variable used for the error info ALSA control
 * according to the return value passed into it. We define any return value
 * other than EIO to imply that CLSIC has returned an error code in a message
 * response, otherwise it is a general driver error.
 *
 */
static inline void set_error_info(struct clsic_vox *vox, int ret)
{
	if (ret == -EIO)
		vox->error_info = VOX_ERROR_DRIVER;
	else
		vox->error_info = VOX_ERROR_CLSIC;
}

/* SoundTrigger HAL-related definitions. */

/* 8 bits in the bitmap */
#define SCC_MANAGE_ACK_MAX_VALUE	255

/* SCC_STATUS... */
/* Indicates VTEx active. */
#define VTE1_ACTIVE			(1 << 8)
#define VTE2_ACTIVE			(1 << 9)
/* Indicates VTEx has triggered since listening state was last entered. */
#define VTE1_TRIGGERED_SINCE_LISTEN	(1 << 12)
#define VTE2_TRIGGERED_SINCE_LISTEN	(1 << 13)
/* Indicates VTEx was responsible for the most recent trigger event. */
#define VTE1_TRIGGERED_MOST_RECENT	(1 << 16)
#define VTE2_TRIGGERED_MOST_RECENT	(1 << 17)

/* SCCMANAGEACKCTRL... */
#define CTRL_ACK_SCC_NULL	0
/* Start VTEx. */
#define CTRL_ACK_START_VTE_1	(1 << 0)
#define CTRL_ACK_START_VTE_2	(1 << 2)
/* Stop VTEx. */
#define CTRL_ACK_STOP_VTE_1	(1 << 1)
#define CTRL_ACK_STOP_VTE_2	(1 << 3)
/* Request streaming mode (without trigger). */
#define CTRL_ACK_STREAM_MODE	(1 << 4)
/* Inform DSP that buffer reading has ended. */
#define CTRL_ACK_STOP_STREAM	(1 << 5)
/* Acknowledge that the VTEx event has been seen. */
#define CTRL_ACK_VTE1_TRIG	(1 << 6)
#define CTRL_ACK_VTE2_TRIG	(1 << 7)

