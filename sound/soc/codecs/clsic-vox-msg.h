/*****************************************************************************
 *
 * Copyright (c) 2018
 * Cirrus Logic, Inc. and Cirrus Logic International Semiconductor Ltd.
 * All rights reserved.
 * This software as well as any related documentation is furnished under
 * license and may only be used or copied in accordance with the terms of the
 * license. The information in this file is furnished for informational use
 * only, is subject to change without notice, and should not be construed as
 * a commitment by Cirrus Logic. Cirrus Logic assumes no responsibility or
 * liability for any errors or inaccuracies that may appear in this document or
 * any software that may be provided in association with this document.
 * Except as permitted by such license, no part of this document may be
 * reproduced, stored in a retrieval system, or transmitted in any form or by
 * any means without the express written consent of Cirrus Logic.
 *
 ******************************************************************************/

#ifndef CLSICMESSAGEDEFINES_VOX_H_
#define CLSICMESSAGEDEFINES_VOX_H_

#include <linux/mfd/clsic/clsicmessagedefines.h>

/**
 *  Service type identifier.
 */
#define CLSIC_SRV_TYPE_VOX		(0x9669)

/**
 *  Service version number.
 */
#define CLSIC_SRV_VERSION_VOX		(0x01010001)

/**
 *  VOX Service message identifiers.
 */
enum clsic_vox_msg_id {
	/**
	 *  VOX messages common to all modes.
	 */
	CLSIC_VOX_MSG_CR_SET_MODE		= 0,
	CLSIC_VOX_MSG_CR_GET_MODE		= 1,
	CLSIC_VOX_MSG_CR_BARGE_IN_ENA		= 2,
	CLSIC_VOX_MSG_CR_BARGE_IN_DIS		= 3,
	CLSIC_VOX_MSG_CR_DEPRECATED_1		= 26,
	CLSIC_VOX_MSG_CR_DEPRECATED_2		= 28,
	CLSIC_VOX_MSG_CR_DEPRECATED_3		= 29,
	CLSIC_VOX_MSG_CR_GET_DI_CATEGORY_COUNT	=
		CLSIC_GBL_MSG_CR_GET_DI_CATEGORY_COUNT,
	CLSIC_VOX_MSG_CR_GET_DI_PAGE_COUNT	=
		CLSIC_GBL_MSG_CR_GET_DI_PAGE_COUNT,
	CLSIC_VOX_MSG_CR_GET_DEBUG_INFO		=
		CLSIC_GBL_MSG_CR_GET_DEBUG_INFO,

	/**
	 *  VOX messages only available in idle mode.
	 */
	CLSIC_VOX_MSG_CR_FACTORY_RESET = 38,

	/**
	 *  VOX Messages for Enrol mode.
	 */
	CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN	= 6,
	CLSIC_VOX_MSG_CR_REP_START		= 7,
	CLSIC_VOX_MSG_N_REP_COMPLETE		= 8,
	CLSIC_VOX_MSG_CR_INSTALL_USER_COMPLETE	= 9,

	/**
	 *  VOX Messages for Listen mode.
	 */
	CLSIC_VOX_MSG_CR_LISTEN_START		= 12,
	CLSIC_VOX_MSG_N_LISTEN_ERR		= 13,
	CLSIC_VOX_MSG_N_TRGR_DETECT		= 14,
	CLSIC_VOX_MSG_CR_SET_TRGR_DETECT	= 15,

	/**
	 *  VOX Messages for Streaming mode.
	 */
	CLSIC_VOX_MSG_CR_AUTH_USER		= 5,
	CLSIC_VOX_MSG_CR_GET_TRGR_INFO		= 16,
	CLSIC_VOX_MSG_CR_GET_AVAIL_ASR_DATA	= 17,
	CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK		= 18,
	CLSIC_VOX_MSG_N_NEW_AUTH_RESULT		= 27,

	/**
	 *  VOX Messages for Manage mode.
	 */
	CLSIC_VOX_MSG_CR_INSTALL_PHRASE		= 22,
	CLSIC_VOX_MSG_CR_IS_PHRASE_INSTALLED	= 20,
	CLSIC_VOX_MSG_CR_REMOVE_PHRASE		= 21,
	CLSIC_VOX_MSG_CR_IS_USER_INSTALLED	= 23,
	CLSIC_VOX_MSG_CR_REMOVE_USER		= 24,
	CLSIC_VOX_MSG_CR_INSTALL_BIN		= 30,
	CLSIC_VOX_MSG_CR_REMOVE_BIN		= 31,
	CLSIC_VOX_MSG_CR_IS_BIN_INSTALLED	= 32,
	CLSIC_VOX_MSG_CR_INSTALL_BIOVTE_MAP	= 33,
	CLSIC_VOX_MSG_CR_REMOVE_BIOVTE_MAP	= 34,
	CLSIC_VOX_MSG_CR_IS_BIOVTE_MAP_INSTALLED = 35,
	CLSIC_VOX_MSG_CR_GET_K2_PUB_KEY		= 36,
	CLSIC_VOX_MSG_CR_SET_HOST_KVPP_KEY	= 37,
};

/**
 *  VOX Service operating modes.
 */
enum clsic_vox_mode {
	CLSIC_VOX_MODE_IDLE	= 0,
	CLSIC_VOX_MODE_MANAGE	= 1,
	CLSIC_VOX_MODE_ENROL	= 2,
	CLSIC_VOX_MODE_LISTEN	= 4,
	CLSIC_VOX_MODE_STREAM	= 5,
};

/**
 *  VOX Service trigger detector domains.
 */
enum clsic_vox_trgr_domain {
	CLSIC_VOX_TRIG_DOMAIN_INTRNL	= 0,
	CLSIC_VOX_TRIG_DOMAIN_EXTRNL	= 1,
};

/**
 *  VOX Service authentication security levels.
 */
enum clsic_vox_security_lvl {
	CLSIC_VOX_SECURITY_LOW		= 0,
	CLSIC_VOX_SECURITY_MEDIUM	= 1,
	CLSIC_VOX_SECURITY_HIGH		= 2,
	CLSIC_VOX_SECURITY_LVL_COUNT	= 3,
};

/**
 *  VOX Service user identifiers.
 */
enum clsic_vox_userid {
	CLSIC_VOX_USER1		= 0,
	CLSIC_VOX_USER2		= 1,
	CLSIC_VOX_USER3		= 2,
	CLSIC_VOX_NO_USER	= 255,
};

/**
 *  VOX Service phrase identifiers.
 */
enum clsic_vox_phraseid {
	CLSIC_VOX_PHRASE_VDT1	= 0,
	CLSIC_VOX_PHRASE_VDT2	= 1,
	CLSIC_VOX_PHRASE_TI	= 4,
};

/**
 *  VOX Service bin identifiers.
 */
enum clsic_vox_binid {
	CLSIC_VOX_BIN_VTE1	= 0,
	CLSIC_VOX_BIN_VTE2	= 1,
	CLSIC_VOX_BIN_SSF	= 2,
	CLSIC_VOX_BIN_CNT	= 3,
};

/**
 *  VOX Service flags that can be or'ed into fields of the
 *  CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN command to change behaviour.
 */
enum clsic_vox_install_user_flags {
	/**
	 *  Flag that can optionally be or'ed into the userid field of the
	 *  CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN command. When present, it
	 *  informs the implementation that combined enrolment should be
	 *  performed, whereby the user enrols for TD and TI at the same time.
	 *  In this case, phrase[1] is used.
	 */
	CLSIC_VOX_USER_FLAG_COMBINED	= 0x80,

	/**
	 *  Flag that can be optionally or'ed into the phraseid field of
	 *  phrase[0] or phrase[1] of the CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN
	 *  command. When specified, tells the implementation that the
	 *  enrolment data generated for that phrase will be discarded. This is
	 *  useful when wanting to enable the combined enrolment UX, but when
	 *  one of the phrases is already enrolled.
	 */
	CLSIC_VOX_PHRASE_FLAG_DISCARD	= 0x80,
};

/**
 *  VOX Service ASR block size in frames.
 */
enum clsic_vox_asr_blk_sz {
	CLSIC_VOX_ASR_BLK_SZ_0		= 0,
	CLSIC_VOX_ASR_BLK_SZ_192	= 1,
	CLSIC_VOX_ASR_BLK_SZ_384	= 2,
	CLSIC_VOX_ASR_BLK_SZ_960	= 3,
	CLSIC_VOX_ASR_BLK_SZ_1920	= 4,
	CLSIC_VOX_ASR_BLK_SZ_4800	= 5,
	CLSIC_VOX_ASR_BLK_SZ_9600	= 6,
};

/**
 *  VOX Service maximum num of authentication results.
 */
enum clsic_vox_max_auth_result_count {
	CLSIC_VOX_MAX_AUTH_RESULT_COUNT = 10,
};

/**
 *  VOX Service flags that determine the format of the authentication
 *  result.
 */
enum clsic_vox_auth_result_format {
	/**
	 *  If this Flag is used then result will be in the format
	 *  specified by struct clsic_vox_auth_result
	 */
	CLSIC_VOX_AUTH_RESULT_CLASSIC	= 0x0,

	/**
	 *  If this Flag is used then result will be in the format
	 *  specified by struct clsic_vox_auth_result_ex
	 */
	CLSIC_VOX_AUTH_RESULT_EXTENDED	= 0x1,

	/**
	 *  If this Flag is used then result will be in the format
	 *  specified by struct clsic_vox_hw_auth_token
	 */
	CLSIC_VOX_AUTH_RESULT_HW_AUTH_TOKEN = 0x2,
};

/**
 *  VOX Service flags send out in CLSIC_VOX_MSG_N_TRGR_DETECT msg.
 *  If no flag is set then biometrics will not be executed, if
 *  CLSIC_VOX_BIOM_TD flag is set then biometrics will be done on
 *  trigger part, if CLSIC_VOX_BIOM_TI flag is set then biometrics
 *  will done on command part. If both CLSIC_VOX_BIOM_TD and
 *  CLSIC_VOX_BIOM_TI are set then biometrics will be done on both
 *  trigger and command parts.
 */
enum clsic_vox_biom_flags {
	CLSIC_VOX_BIOM_TD	= 0x1,
	CLSIC_VOX_BIOM_TI	= 0x2,
};

/**
 *  Bulk part of CLSIC_VOX_MSG_CR_GET_TRGR_INFO response
 */
struct clsic_vox_trgr_info {
	uint8_t phraseid;
	uint8_t engineid;
	int64_t start_time;
	int64_t stop_time;
	int64_t now_time;
	uint64_t timer_freq;
	uint8_t pad[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER command.
 */
struct clsic_vox_auth_challenge {
	uint8_t nonce[16];
} PACKED;


/**
 *  This is the core part of the biometric result that is sent out in
 *  bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER bulk response.
 *
 *  .result_format conveys the exact format of the result i.e.
 *  CLSIC_VOX_AUTH_RESULT_CLASSIC (or) CLSIC_VOX_AUTH_RESULT_EXTENDED.
 *
 *  .nonce is the 16 byte challenge that is sent by host as a part of
 *  CLSIC_VOX_MSG_CR_AUTH_USER command.
 *
 *  .security_lvl conveys one of the 3 security levels at which host had
 *  requested the result using CLSIC_VOX_MSG_CR_AUTH_USER command.
 *
 *  If result_count is greater than CLSIC_VOX_MAX_AUTH_RESULT_COUNT
 *  then only the last CLSIC_VOX_MAX_AUTH_RESULT_COUNT will be available
 *  in the below structure else result_count number of results will be
 *  available.
 *
 *  Results are sorted in ascending order of time i.e. start_frame[i+1]
 *  will be greater than end_frame[i].
 *
 *  .userid[i] conveys the id of the user identified in each audio section
 *  defined by start_frame[i] and end_frame[i]
 *
 *  .sha contains the 256 bit sha of the audio samples used to generate
 *  each result.
 *
 *  The "secure_audio_src" field is a bit field where each of bits [0:9] are
 *  used to represent the security of each result segment. 0 means that the
 *  audio for the segment was sourced from a non-secure audio source. 1 means
 *  that the audio for the segment was sourced from a secure audio source.
 */
struct clsic_vox_auth_result_core {
	uint8_t result_format;
	uint8_t nonce[16];
	uint8_t security_lvl;
	int32_t result_count;
	int32_t start_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	int32_t end_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	uint8_t userid[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	uint8_t sha[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][32];
	uint16_t secure_audio_src;
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER response when
 *  blkcmd_auth_user.result_format is CLSIC_VOX_AUTH_RESULT_CLASSIC
 *
 */
struct clsic_vox_auth_result {
	struct clsic_vox_auth_result_core core;
	uint8_t pad1[14];
	uint8_t signature[74];
	uint8_t pad2[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER response when
 *  blkcmd_auth_user.result_format is CLSIC_VOX_AUTH_RESULT_EXTENDED
 *
 *  .score gives scores for all installed users even if no user was identified
 *
 *  If .is_spoof is set that would mean that user identified in .userid may
 *  be because of spoof or recorded audio rather than real live audio spoken
 *  by a person and .as_score gives the raw score of the antispoofing algorithm
 */
struct clsic_vox_auth_result_ex {
	struct clsic_vox_auth_result_core core;
	float score[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint8_t is_spoof[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	float as_score[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint8_t signature[74];
	uint8_t pad[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_GET_K2_PUB_KEY response.
 */
struct clsic_vox_k2_pub_key {
	uint8_t pub_key[33];
	uint8_t pad1[2];
	uint8_t signature_valid;
	uint8_t signature[74];
	uint8_t pad2[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_SET_HOST_KVPP_KEY command.
 */
struct clsic_vox_host_kvpp_key {
	uint8_t pub_key[33];
	uint8_t pad[3];
} PACKED;


/**
 *  Part of clsic_vox_biovte_map
 */
struct clsic_vox_biovte_map_entry {
	int32_t bio_phraseid;
	int32_t vte_engineid;
	int32_t vte_phraseid;
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_INSTALL_BIOVTE_MAP command,
 *  map will have cnt number of entries.
 */
struct clsic_vox_biovte_map {
	uint32_t cnt;
	struct clsic_vox_biovte_map_entry map[0];
} PACKED;

/**
 *  Bulk part of CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN command.
 *
 *  In order to send a Security package Host and Clsic must have
 *  previously exchanged their public keys using GET_K2_PUB_KEY
 *  and SET_HOST_KVPP_KEY messages.
 *
 *  Host first encrypts the security package. Everything starting
 *  from .timestamp and up to and including .hmac_key is encrypted
 *  using ECDH cryptography. ECDH requires an ephemeral key which
 *  gets generated at the host and public part of it gets conveyed
 *  to Clsic in .eph_key as plain (unencrypted) text.
 *
 *  After encrypting Host signs the security package. Everything
 *  starting from .eph_key and up to and including .pad1 is signed
 *  and the resulting signature is put in .signature
 *
 */
struct clsic_vox_security_package {
	uint8_t  eph_key[33];
	uint64_t timestamp;
	uint8_t  hmac_key[32];
	uint8_t  pad1[23];
	uint8_t  signature[74];
	uint8_t  pad2[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN response.
 */
struct clsic_vox_install_usr_challenge {
	uint64_t challenge;
} PACKED;

/**
 *  Bulk part of CLSIC_VOX_MSG_CR_REP_START.
 *
 *  (or)
 *
 *  Bulk part of CLSIC_VOX_MSG_CR_AUTH_USER response, when
 *  blkcmd_auth_user.result_format is CLSIC_VOX_AUTH_RESULT_HW_AUTH_TOKEN
 *
 *  This is a standard android hardware authentication token described at
 *  https://source.android.com/security/authentication
 *
 *  The hmac in the token is generated using the hmac_key previously conveyed to
 *  clsic as a part of clsic_vox_security_package.
 *
 */
struct clsic_vox_hw_auth_token {
	uint8_t version;
	uint64_t challenge;
	uint64_t suid;
	uint64_t auth_id;
	uint32_t auth_type;
	uint64_t timestamp;
	uint8_t hmac[32];
	uint8_t pad[3];
} PACKED;


/**
 *  Bulk part of CLSIC_VOX_MSG_CR_AUTH_USER command when
 *  blkcmd_auth_user.result_format is
 *  CLSIC_VOX_AUTH_RESULT_HW_AUTH_TOKEN
 */
struct clsic_vox_hw_auth_challenge {
	struct clsic_vox_security_package sp;
	uint64_t challenge;
} PACKED;

/**
 *  VOX Service messages.
 */
union clsic_vox_msg {
	uint8_t raw_msg[CLSIC_FIXED_MSG_SZ];


	/**
	 *  VOX messages common to all modes.
	 */


	/**
	 *  CLSIC_VOX_MSG_CR_SET_MODE command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t mode;
	} PACKED cmd_set_mode;

	/**
	 *  CLSIC_VOX_MSG_CR_SET_MODE response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_set_mode;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_MODE command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_mode;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_MODE response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint8_t mode;
	} PACKED rsp_get_mode;

	/**
	 *  CLSIC_VOX_MSG_CR_BARGE_IN_ENA command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_barge_in_ena;

	/**
	 *  CLSIC_VOX_MSG_CR_BARGE_IN_ENA response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_barge_in_ena;

	/**
	 *  CLSIC_VOX_MSG_CR_BARGE_IN_DIS command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_barge_in_dis;

	/**
	 *  CLSIC_VOX_MSG_CR_BARGE_IN_DIS response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_barge_in_dis;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DI_CATEGORY_COUNT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_di_category_count;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DI_CATEGORY_COUNT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category_count;
	} PACKED rsp_get_di_category_count;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DI_PAGE_COUNT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint16_t category;
	} PACKED cmd_get_di_page_count;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DI_PAGE_COUNT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category;
		uint16_t page_count;
	} PACKED rsp_get_di_page_count;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DEBUG_INFO command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED cmd_get_debug_info;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DEBUG_INFO response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED rsp_get_debug_info;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_DEBUG_INFO response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED blkrsp_get_debug_info;


	/**
	 *  VOX Messages for Enrol mode.
	 */


	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
		uint8_t userid;
		uint8_t phraseid[2];
		uint16_t timeout_ms;
	} PACKED blkcmd_install_user_begin;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
		uint16_t duration_ms;
		uint8_t rep_count;
	} PACKED blkrsp_install_user_begin;

	/**
	 *  CLSIC_VOX_MSG_CR_REP_START command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_rep_start;

	/**
	 *  CLSIC_VOX_MSG_CR_REP_START response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_rep_start;

	/**
	 *  CLSIC_VOX_MSG_N_REP_COMPLETE notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t err;
	} PACKED nty_rep_complete;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_USER_COMPLETE command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint64_t tag;
	} PACKED cmd_install_user_complete;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_USER_COMPLETE response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_install_user_complete;


	/**
	 *  VOX Messages for Listen mode.
	 */


	/**
	 *  CLSIC_VOX_MSG_CR_LISTEN_START command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t trgr_domain;
		uint8_t asr_blk_sz;
		uint8_t biom_disable;
	} PACKED cmd_listen_start;

	/**
	 *  CLSIC_VOX_MSG_CR_LISTEN_START response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint8_t asr_ch_count;
	} PACKED rsp_listen_start;

	/**
	 *  CLSIC_VOX_MSG_N_LISTEN_ERR notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t err;
	} PACKED nty_listen_err;

	/**
	 *  CLSIC_VOX_MSG_N_TRGR_DETECT notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t biom_flags;
	} PACKED nty_trgr_detect;

	/**
	 *  CLSIC_VOX_MSG_CR_SET_TRGR_DETECT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		int32_t vte_engineid;
		int32_t vte_phraseid;
	} PACKED cmd_set_trgr_detect;

	/**
	 *  CLSIC_VOX_MSG_CR_SET_TRGR_DETECT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_set_trgr_detect;


	/**
	 *  VOX Messages for Streaming mode.
	 */


	/**
	 *  CLSIC_VOX_MSG_CR_AUTH_USER command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
		uint8_t security_lvl;
		uint8_t result_format;
	} PACKED blkcmd_auth_user;

	/**
	 *  CLSIC_VOX_MSG_CR_AUTH_USER response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_auth_user;

	/**
	 *  CLSIC_VOX_MSG_CR_AUTH_USER response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_auth_user;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_TRGR_INFO command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_trgr_info;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_TRGR_INFO response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_get_trgr_info;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_TRGR_INFO response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_get_trgr_info;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_AVAIL_ASR_DATA command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_avail_asr_data;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_AVAIL_ASR_DATA response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint32_t data_sz;
	} PACKED rsp_get_avail_asr_data;

	/**
	 *  CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_asr_block;

	/**
	 *  CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK acknowledgement structure.
	 */
	struct {
		struct clsic_ack_hdr hdr;
	} PACKED ack_get_asr_block;

	/**
	 *  CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_get_asr_block;

	/**
	 *  CLSIC_VOX_MSG_CRA_GET_ASR_BLOCK response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_get_asr_block;

	/**
	 *  CLSIC_VOX_MSG_N_NEW_AUTH_RESULT notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		int32_t total_frames_processed;
		uint8_t userid[CLSIC_VOX_SECURITY_LVL_COUNT];
		uint8_t auth_stop_reason;
	} PACKED nty_new_auth_result;


	/**
	 *  VOX Messages for Manage mode.
	 */


	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_PHRASE command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
		uint8_t phraseid;
	} PACKED cmd_install_phrase;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_PHRASE response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_install_phrase;

	/**
	 *  CLSIC_VOX_MSG_CR_IS_PHRASE_INSTALLED command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t phraseid;
	} PACKED cmd_is_phrase_installed;

	/**
	 *  CLSIC_VOX_MSG_CR_IS_PHRASE_INSTALLED response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_is_phrase_installed;

	/**
	 *  CLSIC_VOX_MSG_CR_REMOVE_PHRASE command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t phraseid;
	} PACKED cmd_remove_phrase;

	/**
	 *  CLSIC_VOX_MSG_CR_REMOVE_PHRASE response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_remove_phrase;

	/**
	 *  CLSIC_VOX_MSG_CR_IS_USER_INSTALLED command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t userid;
		uint8_t phraseid;
	} PACKED cmd_is_user_installed;

	/**
	 *  CLSIC_VOX_MSG_CR_IS_USER_INSTALLED response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint64_t tag;
	} PACKED rsp_is_user_installed;

	/**
	 *  CLSIC_VOX_MSG_CR_REMOVE_USER command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t userid;
		uint8_t phraseid;
	} PACKED cmd_remove_user;

	/**
	 *  CLSIC_VOX_MSG_CR_REMOVE_USER response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_remove_user;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_K2_PUB_KEY command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_k2_pub_key;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_K2_PUB_KEY response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_get_k2_pub_key;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_K2_PUB_KEY response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_get_k2_pub_key;

	/**
	 *  CLSIC_VOX_MSG_CR_SET_HOST_KVPP_KEY command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_set_host_kvpp_key;

	/**
	 *  CLSIC_VOX_MSG_CR_SET_HOST_KVPP_KEY response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint8_t is_rate_limited;
	} PACKED rsp_set_host_kvpp_key;

	/**
	 *  CLSIC_VOX_MSG_CR_FACTORY_RESET command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_factory_reset;

	/**
	 *  CLSIC_VOX_MSG_CR_FACTORY_RESET response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_factory_reset;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_BIN command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
		uint8_t binid;
	} PACKED blkcmd_install_bin;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_BIN response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_install_bin;

	/**
	 *  CLSIC_VOX_MSG_CR_IS_BIN_INSTALLED command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t binid;
	} PACKED cmd_is_bin_installed;

	/**
	 *  CLSIC_VOX_MSG_CR_IS_BIN_INSTALLED response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_is_bin_installed;

	/**
	 *  CLSIC_VOX_MSG_CR_REMOVE_BIN command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t binid;
	} PACKED cmd_remove_bin;

	/**
	 *  CLSIC_VOX_MSG_CR_REMOVE_BIN response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_remove_bin;

	/**
	 * CLSIC_VOX_MSG_CR_INSTALL_BIOVTE_MAP command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_install_biovte_map;

	/**
	 * CLSIC_VOX_MSG_CR_INSTALL_BIOVTE_MAP response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_install_biovte_map;

	/**
	 * CLSIC_VOX_MSG_CR_IS_BIOVTE_MAP_INSTALLED command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_is_biovte_map_installed;

	/**
	 * CLSIC_VOX_MSG_CR_IS_BIOVTE_MAP_INSTALLED response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_is_biovte_map_installed;

	/**
	 * CLSIC_VOX_MSG_CR_REMOVE_BIOVTE_MAP command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_remove_biovte_map;

	/**
	 * CLSIC_VOX_MSG_CR_REMOVE_BIOVTE_MAP response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_remove_biovte_map;
} PACKED;

#endif /* CLSICMESSAGEDEFINES_VOX_H_ */
