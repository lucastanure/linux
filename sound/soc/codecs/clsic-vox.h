/*
 * Copyright (C) 2017 Cirrus Logic, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define VOX_ASR_MIN_FRAGMENT_SZ	0
#define VOX_ASR_MAX_FRAGMENT_SZ	307200
#define VOX_ASR_MIN_FRAGMENTS	1
#define VOX_ASR_MAX_FRAGMENTS	256

#define VOX_MAX_USERS		3
#define VOX_MAX_PHRASES		5

#define VOX_NUM_NEW_KCONTROLS	21

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
 * @listen_error:	In case of a failure to trigger, this error is set to
 *			prevent streaming from occurring.
 * @error:		If there is a problem in setting up the ASR stream
 *			before or after trigger has occurred, this is set to
 *			prevent further attempts to stream data.
 * @asr_block_pending:	Set while we are waiting to receive an ASR block.
 * @wait_for_trigger:	Thread to wait for the initial trigger.
 * @trigger_heard:	Completion that is used in waiting for a trigger to
 *			occur.
 * @asr_block_completion:	Used with asr_block_pending if the stream is
 *				closed mid ASR block copy.
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
	bool listen_error;

	bool error;
	bool asr_block_pending;

	struct task_struct *wait_for_trigger;
	struct completion trigger_heard;
	struct completion asr_block_completion;
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
	struct mutex mgmt_mode_lock;
	/* mgmt_mode refers to ongoing vox biometric operations only. */
	unsigned int mgmt_mode;
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
	struct soc_mixer_control clsic_error_mixer_ctrl;

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

#define VOX_NUM_MGMT_MODES			23

#define VOX_MGMT_MODE_NEUTRAL			0
#define VOX_MGMT_MODE_INSTALL_ASSET		1
#define VOX_MGMT_MODE_INSTALLING_ASSET		2
#define VOX_MGMT_MODE_UNINSTALL_ASSET		3
#define VOX_MGMT_MODE_UNINSTALLING_ASSET	4
#define VOX_MGMT_MODE_REMOVE_USER		5
#define VOX_MGMT_MODE_REMOVING_USER		6
#define VOX_MGMT_MODE_START_ENROL		7
#define VOX_MGMT_MODE_STARTING_ENROL		8
#define VOX_MGMT_MODE_ENROLLING			9
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
#define VOX_MGMT_MODE_STARTING_LISTEN		20
#define VOX_MGMT_MODE_LISTENING			21
#define VOX_MGMT_MODE_STREAMING			22

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
	[VOX_MGMT_MODE_ENROLLING]		= "Enrolling User",
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
	[VOX_MGMT_MODE_STARTING_LISTEN]		= "Starting Listen For Trigger",
	[VOX_MGMT_MODE_LISTENING]		= "Listening For Trigger",
	[VOX_MGMT_MODE_STREAMING]		= "Streaming ASR Data",
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
