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

#define VOX_NUM_ERRORS			10

#define VOX_ERROR_SUCCESS		0
#define VOX_ERROR_LIBRARY		1
#define VOX_ERROR_TIMEOUT		2
#define VOX_ERROR_BAD_ASSET		3
#define VOX_ERROR_DISABLE_BARGE_IN	4
#define VOX_ERROR_MORE_SPEECH_NEEDED	5
#define VOX_ERROR_TOO_LOUD		6
#define VOX_ERROR_TOO_NOISY		7
#define VOX_ERROR_NO_USERS		8
#define VOX_ERROR_CLEARED		9

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

/* Translation of CLSIC errors into strings. */

struct clsic_response_codes_struct {
	char name[64];
	int code;
};

struct clsic_response_codes_struct clsic_response_codes[] = {
	{"CLSIC_ERR_NONE", CLSIC_ERR_NONE},
	{"CLSIC_ERR_NO_MEM", CLSIC_ERR_NO_MEM},
	{"CLSIC_ERR_INVAL_SI", CLSIC_ERR_INVAL_SI},
	{"CLSIC_ERR_INVAL_MSGID", CLSIC_ERR_INVAL_MSGID},
	{"CLSIC_ERR_INVAL_CRAN", CLSIC_ERR_INVAL_CRAN},
	{"CLSIC_ERR_INVAL_BULK", CLSIC_ERR_INVAL_BULK},
	{"CLSIC_ERR_CANCELLED", CLSIC_ERR_CANCELLED},
	{"CLSIC_ERR_INVAL_ARG", CLSIC_ERR_INVAL_ARG},
	{"CLSIC_ERR_INVAL_ADDR", CLSIC_ERR_INVAL_ADDR},
	{"CLSIC_ERR_ACCESS", CLSIC_ERR_ACCESS},
	{"CLSIC_ERR_HW", CLSIC_ERR_HW},
	{"CLSIC_ERR_FLASH", CLSIC_ERR_FLASH},
	{"CLSIC_ERR_TOO_SMALL", CLSIC_ERR_TOO_SMALL},
	{"CLSIC_ERR_WAKELOCK_HELD", CLSIC_ERR_WAKELOCK_HELD},
	{"CLSIC_ERR_INVAL_MODE", CLSIC_ERR_INVAL_MODE},
	{"CLSIC_ERR_INVAL_MODE_TRANSITION", CLSIC_ERR_INVAL_MODE_TRANSITION},
	{"CLSIC_ERR_INVAL_CMD_FOR_MODE", CLSIC_ERR_INVAL_CMD_FOR_MODE},
	{"CLSIC_ERR_INVAL_USERID", CLSIC_ERR_INVAL_USERID},
	{"CLSIC_ERR_INVAL_PHRASEID", CLSIC_ERR_INVAL_PHRASEID},
	{"CLSIC_ERR_INVAL_APP_ID", CLSIC_ERR_INVAL_APP_ID},
	{"CLSIC_ERR_USER_NOT_INSTALLED", CLSIC_ERR_USER_NOT_INSTALLED},
	{"CLSIC_ERR_USER_ALREADY_INSTALLED", CLSIC_ERR_USER_ALREADY_INSTALLED},
	{"CLSIC_ERR_PHRASE_NOT_INSTALLED", CLSIC_ERR_PHRASE_NOT_INSTALLED},
	{"CLSIC_ERR_VOICEID", CLSIC_ERR_VOICEID},
	{"CLSIC_ERR_INPUT_PATH", CLSIC_ERR_INPUT_PATH},
	{"CLSIC_ERR_SECURITY_FAIL", CLSIC_ERR_SECURITY_FAIL},
	{"CLSIC_ERR_AUTH_ABORT_BARGE_IN", CLSIC_ERR_AUTH_ABORT_BARGE_IN},
	{"CLSIC_ERR_AUTH_NOT_STARTED_BARGE_IN",
		CLSIC_ERR_AUTH_NOT_STARTED_BARGE_IN},
	{"CLSIC_ERR_INVAL_SECURITY_LVL", CLSIC_ERR_INVAL_SECURITY_LVL},
	{"CLSIC_ERR_NO_USER_IDENTIFIED", CLSIC_ERR_NO_USER_IDENTIFIED},
	{"CLSIC_ERR_NOT_INSTALLING_USER", CLSIC_ERR_NOT_INSTALLING_USER},
	{"CLSIC_ERR_ALREADY_INSTALLING_USER",
		CLSIC_ERR_ALREADY_INSTALLING_USER},
	{"CLSIC_ERR_INVAL_REP_COUNT", CLSIC_ERR_INVAL_REP_COUNT},
	{"CLSIC_ERR_ONGOING_REP", CLSIC_ERR_ONGOING_REP},
	{"CLSIC_ERR_REPS_COMPLETE", CLSIC_ERR_REPS_COMPLETE},
	{"CLSIC_ERR_REP_TRGR_TIMEOUT", CLSIC_ERR_REP_TRGR_TIMEOUT},
	{"CLSIC_ERR_REP_UNEXPECTED_TRGR", CLSIC_ERR_REP_UNEXPECTED_TRGR},
	{"CLSIC_ERR_REP_NOISE_LVL", CLSIC_ERR_REP_NOISE_LVL},
	{"CLSIC_ERR_REP_SPEECH_RATIO", CLSIC_ERR_REP_SPEECH_RATIO},
	{"CLSIC_ERR_REP_NET_SPEECH", CLSIC_ERR_REP_NET_SPEECH},
	{"CLSIC_ERR_REP_SATURATION", CLSIC_ERR_REP_SATURATION},
	{"CLSIC_ERR_REP_FEATURE_OVERFLOW", CLSIC_ERR_REP_FEATURE_OVERFLOW},
	{"CLSIC_ERR_REPS_NOT_ENOUGH_VALID", CLSIC_ERR_REPS_NOT_ENOUGH_VALID},
	{"CLSIC_ERR_AUTH_IN_PROGRESS", CLSIC_ERR_AUTH_IN_PROGRESS},
	{"CLSIC_ERR_INVAL_TRGR_DOMAIN", CLSIC_ERR_INVAL_TRGR_DOMAIN},
	{"CLSIC_ERR_INVAL_ASR_BLOCK_SZ", CLSIC_ERR_INVAL_ASR_BLOCK_SZ},
	{"CLSIC_ERR_ALREADY_LISTENING", CLSIC_ERR_ALREADY_LISTENING},
	{"CLSIC_ERR_INVAL_CMD_FOR_TRGR_DOMAIN",
		CLSIC_ERR_INVAL_CMD_FOR_TRGR_DOMAIN},
	{"CLSIC_ERR_LISTEN_NOT_STARTED", CLSIC_ERR_LISTEN_NOT_STARTED},
	{"CLSIC_ERR_INVAL_TRGR_INFO", CLSIC_ERR_INVAL_TRGR_INFO},
	{"CLSIC_ERR_INVAL_TRGR_ENGINEID", CLSIC_ERR_INVAL_TRGR_ENGINEID},
	{"CLSIC_ERR_INVAL_TRGR_PHRASEID", CLSIC_ERR_INVAL_TRGR_PHRASEID},
	{"CLSIC_ERR_ASR_PREV_REQUEST_PENDING",
		CLSIC_ERR_ASR_PREV_REQUEST_PENDING},
	{"CLSIC_ERR_ASR_STREAM", CLSIC_ERR_ASR_STREAM},
	{"CLSIC_ERR_ASR_STREAM_DISABLED", CLSIC_ERR_ASR_STREAM_DISABLED},
	{"CLSIC_ERR_KEY_NOT_FOUND", CLSIC_ERR_KEY_NOT_FOUND},
	{"CLSIC_ERR_BPB_SZ_TOO_SMALL", CLSIC_ERR_BPB_SZ_TOO_SMALL},
	{"CLSIC_ERR_BPB_SZ_UNALIGNED", CLSIC_ERR_BPB_SZ_UNALIGNED},
	{"CLSIC_ERR_BPB_BAD_HDR", CLSIC_ERR_BPB_BAD_HDR},
	{"CLSIC_ERR_BPB_BAD_IMGMAP", CLSIC_ERR_BPB_BAD_IMGMAP},
	{"CLSIC_ERR_BPB_SZ_INCONSISTENT", CLSIC_ERR_BPB_SZ_INCONSISTENT},
	{"CLSIC_ERR_BPB_AUTH_FAILED", CLSIC_ERR_BPB_AUTH_FAILED},
	{"CLSIC_ERR_COV_DISABLED", CLSIC_ERR_COV_DISABLED},
	{"CLSIC_ERR_INVALID_ENROL_DURATION", CLSIC_ERR_INVALID_ENROL_DURATION},
	{"CLSIC_ERR_INVALID_AUTH_RESULT_FORMAT",
		CLSIC_ERR_INVALID_AUTH_RESULT_FORMAT},
	{"CLSIC_ERR_BPB_ASSET_INVAL_VER", CLSIC_ERR_BPB_ASSET_INVAL_VER},
	{"CLSIC_ERR_BPB_ASSET_INVAL_SZ", CLSIC_ERR_BPB_ASSET_INVAL_SZ},
	{"CLSIC_ERR_BPB_ASSET_INVAL_COMP_TYPE",
		CLSIC_ERR_BPB_ASSET_INVAL_COMP_TYPE},
	{"CLSIC_ERR_BPB_ASSET_INVAL_COMP_TABLE_SZ",
		CLSIC_ERR_BPB_ASSET_INVAL_COMP_TABLE_SZ},
	{"CLSIC_ERR_BPB_ASSET_INVAL_FLAGS", CLSIC_ERR_BPB_ASSET_INVAL_FLAGS},
	{"CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED",
		CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED},
	{"CLSIC_ERR_AUTH_NO_USERS_TO_MATCH", CLSIC_ERR_AUTH_NO_USERS_TO_MATCH},
	{"CLSIC_ERR_AUTH_BIOM_DISABLED", CLSIC_ERR_AUTH_BIOM_DISABLED},
	{"CLSIC_ERR_REP_PLOSIVE", CLSIC_ERR_REP_PLOSIVE},
	{"CLSIC_ERR_REP_SNR", CLSIC_ERR_REP_SNR},
	{"CLSIC_ERR_REP_REWIND_OVF", CLSIC_ERR_REP_REWIND_OVF},
	{"CLSIC_ERR_INVALID_BIN_ID", CLSIC_ERR_INVALID_BIN_ID},
	{"CLSIC_ERR_INVALID_BIN_DATA", CLSIC_ERR_INVALID_BIN_DATA},
	{"CLSIC_ERR_BIN_NOT_INSTALLED", CLSIC_ERR_BIN_NOT_INSTALLED},
	{"CLSIC_ERR_BIOVTE_MAP_INVALID", CLSIC_ERR_BIOVTE_MAP_INVALID},
	{"CLSIC_ERR_BIOVTE_MAP_SZ_INVALID", CLSIC_ERR_BIOVTE_MAP_SZ_INVALID},
	{"CLSIC_ERR_BIOVTE_MAP_NOT_INSTALLED",
		CLSIC_ERR_BIOVTE_MAP_NOT_INSTALLED},
	{"CLSIC_ERR_BIOVTE_MAPPING_DOES_NOT_EXIST",
		CLSIC_ERR_BIOVTE_MAPPING_DOES_NOT_EXIST},
	{"CLSIC_ERR_BL_AUTH_FAILED", CLSIC_ERR_BL_AUTH_FAILED},
	{"CLSIC_ERR_BL_INVAL_VERSION", CLSIC_ERR_BL_INVAL_VERSION},
	{"CLSIC_ERR_BL_FLASH_WRITE_FAILED", CLSIC_ERR_BL_FLASH_WRITE_FAILED},
	{"CLSIC_ERR_BL_ARB_CHECK_FAILED", CLSIC_ERR_BL_ARB_CHECK_FAILED},
	{"CLSIC_ERR_BL_CLUB_TOO_LARGE", CLSIC_ERR_BL_CLUB_TOO_LARGE},
	{"CLSIC_ERR_BL_IMG_NAME_CLASH", CLSIC_ERR_BL_IMG_NAME_CLASH},
	{"CLSIC_ERR_BL_CAB_NOT_1ST_IN_MAB", CLSIC_ERR_BL_CAB_NOT_1ST_IN_MAB},
	{"CLSIC_ERR_BL_TOO_MANY_IMGS", CLSIC_ERR_BL_TOO_MANY_IMGS},
	{"CLSIC_ERR_BL_NO_MIN_SET_IN_MAB", CLSIC_ERR_BL_NO_MIN_SET_IN_MAB},
	{"CLSIC_ERR_BL_FLASH_ERASE_FAILED", CLSIC_ERR_BL_FLASH_ERASE_FAILED},
	{"CLSIC_ERR_BL_FLASH_READ_FAILED", CLSIC_ERR_BL_FLASH_READ_FAILED},
	{"CLSIC_ERR_BL_NBS2_NOT_1ST_IN_CAB", CLSIC_ERR_BL_NBS2_NOT_1ST_IN_CAB},
	{"CLSIC_ERR_BL_OSAPP_NOT_2ND_IN_CAB",
		CLSIC_ERR_BL_OSAPP_NOT_2ND_IN_CAB},
};
