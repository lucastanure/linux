/*****************************************************************************
*
* Copyright (c) 2017 Cirrus Logic International (UK) Ltd.
* All rights reserved.
*
* This software as well as any related documentation is furnished under
* license and may only be used or copied in accordance with the terms of the
* license. The information in this file is furnished for informational use
* only, is subject to change without notice, and should not be construed as
* a commitment by Cirrus Logic International (UK) Ltd.  Cirrus Logic
* International (UK) Ltd assumes no responsibility or liability for any
* errors or inaccuracies that may appear in this document or any software
* that may be provided in association with this document.
*
* Except as permitted by such license, no part of this document may be
* reproduced, stored in a retrieval system, or transmitted in any form or by
* any means without the express written consent of Cirrus Logic
* International (UK) Ltd or affiliated companies.
*
******************************************************************************/

#ifndef CLSICMESSAGEDEFINES_H
#define CLSICMESSAGEDEFINES_H

/**
 *  Size in bytes of a fixed size message.
 */
#define CLSIC_FIXED_MSG_SZ			(12)

/**
 *  Service type identifiers.
 */
#define CLSIC_SRV_TYPE_SYS			(0x5953)
#define CLSIC_SRV_TYPE_RAS			(0x4152)
#define CLSIC_SRV_TYPE_VOX			(0x9669)
#define CLSIC_SRV_TYPE_DBG			(0x4244)
#define CLSIC_SRV_TYPE_BLD			(0x4C42)

/**
 *  Service version numbers.
 */
#define CLSIC_SRV_VERSION_SYS			(CONFIG_VERSION_NUMBER)
#define CLSIC_SRV_VERSION_RAS			(0x00020000)
#define CLSIC_SRV_VERSION_VOX			(0x00040001)
#define CLSIC_SRV_VERSION_DBG			(0x00020000)
#define CLSIC_SRV_VERSION_BLD			(0x01000000)

/**
 *  Well-known service instance indexes.
 */
#define CLSIC_SRV_INST_SYS			(0x00)
#define CLSIC_SRV_INST_BLD			(0x1F)

/**
 *  Values for the 2 bit CRAN field embedded in the header of all messages.
 */
#define CLSIC_CRAN_CMD				(0)
#define CLSIC_CRAN_RSP				(1)
#define CLSIC_CRAN_ACK				(2)
#define CLSIC_CRAN_NTY				(3)

/**
 *  Masks and positions for the individual fields in a
 *  serviceInstance-Bulk-Cran (sbc) field.
 */
#define CLSIC_SRV_INST_POS			(3)
#define CLSIC_SRV_INST_MASK			(0x1F << CLSIC_SRV_INST_POS)
#define CLSIC_BULK_POS				(2)
#define CLSIC_BULK_MASK				(0x1 << CLSIC_BULK_POS)
#define CLSIC_CRAN_POS				(0)
#define CLSIC_CRAN_MASK				(0x3 << CLSIC_CRAN_POS)

/**
 *  Given a pointer to a ServiceIntance-Bulk-Cran (sbc) field, sets to the
 *  instance to the provided value.
 */
static inline void clsic_set_srv_inst(uint8_t *sbc, uint8_t inst) {
	inst = (inst) & (CLSIC_SRV_INST_MASK >> CLSIC_SRV_INST_POS);
	*sbc = (*sbc) & (~CLSIC_SRV_INST_MASK);
	*sbc = (*sbc) | (inst << CLSIC_SRV_INST_POS);
}

/**
 *  Extracts and returns the instance from the provided
 *  ServiceIntance-Bulk-Cran (sbc) field.
 */
static inline uint8_t clsic_get_srv_inst(uint8_t sbc) {
	return (sbc & CLSIC_SRV_INST_MASK) >> CLSIC_SRV_INST_POS;
}

/**
 *  Given a pointer to a ServiceIntance-Bulk-Cran (sbc) field, sets to the
 *  bulk bit to the provided value.
 */
static inline void clsic_set_bulk(uint8_t *sbc, uint8_t bulk) {
	bulk = (bulk) & (CLSIC_BULK_MASK >> CLSIC_BULK_POS);
	*sbc = (*sbc) & (~CLSIC_BULK_MASK);
	*sbc = (*sbc) | (bulk << CLSIC_BULK_POS);
}

/**
 *  Extracts and returns the bulk bit from the provided
 *  ServiceIntance-Bulk-Cran (sbc) field.
 */
static inline uint8_t clsic_get_bulk_bit(uint8_t sbc) {
	return (sbc & CLSIC_BULK_MASK) >> CLSIC_BULK_POS;
}

/**
 *  Given a pointer to a ServiceIntance-Bulk-Cran (sbc) field, sets to the
 *  CRAN to the provided value.
 */
static inline void clsic_set_cran(uint8_t *sbc, uint8_t cran) {
	cran = (cran) & (CLSIC_CRAN_MASK >> CLSIC_CRAN_POS);
	*sbc = (*sbc) & (~CLSIC_CRAN_MASK);
	*sbc = (*sbc) | (cran << CLSIC_CRAN_POS);
}

/**
 *  Extracts and returns the CRAN from the provided
 *  ServiceIntance-Bulk-Cran (sbc) field.
 */
static inline uint8_t clsic_get_cran(uint8_t sbc) {
	return (sbc & CLSIC_CRAN_MASK) >> CLSIC_CRAN_POS;
}

/**
 *  Error codes returned in response message err fields.
 */
enum clsic_err {
	CLSIC_ERR_NONE				= 0,
	CLSIC_ERR_NO_MEM			= 1,
	CLSIC_ERR_INVAL_SI			= 2,
	CLSIC_ERR_INVAL_MSGID			= 3,
	CLSIC_ERR_INVAL_CRAN			= 4,
	CLSIC_ERR_INVAL_BULK			= 5,
	CLSIC_ERR_CANCELLED			= 6,
	CLSIC_ERR_INVAL_ARG			= 7,
	CLSIC_ERR_INVAL_ADDR			= 8,
	CLSIC_ERR_ACCESS			= 9,
	CLSIC_ERR_HW				= 10,
	CLSIC_ERR_FLASH				= 11,
	CLSIC_ERR_TOO_SMALL			= 12,
	CLSIC_ERR_WAKELOCK_HELD			= 13,
	CLSIC_ERR_INVAL_MODE			= 14,
	CLSIC_ERR_INVAL_MODE_TRANSITION		= 15,
	CLSIC_ERR_INVAL_CMD_FOR_MODE		= 16,
	CLSIC_ERR_INVAL_USERID			= 17,
	CLSIC_ERR_INVAL_PHRASEID		= 18,
	CLSIC_ERR_INVAL_APP_ID			= 19,
	CLSIC_ERR_USER_NOT_INSTALLED		= 21,
	CLSIC_ERR_USER_ALREADY_INSTALLED	= 23,
	CLSIC_ERR_PHRASE_NOT_INSTALLED		= 24,
	CLSIC_ERR_VOICEID			= 25,
	CLSIC_ERR_INPUT_PATH			= 26,
	CLSIC_ERR_SECURITY_FAIL			= 27,
	CLSIC_ERR_AUTH_ABORT_BARGE_IN		= 28,
	CLSIC_ERR_AUTH_NOT_STARTED_BARGE_IN	= 29,
	CLSIC_ERR_INVAL_SECURITY_LVL		= 30,
	CLSIC_ERR_NO_USER_IDENTIFIED		= 31,
	CLSIC_ERR_NOT_INSTALLING_USER		= 33,
	CLSIC_ERR_ALREADY_INSTALLING_USER	= 34,
	CLSIC_ERR_INVAL_REP_COUNT		= 35,
	CLSIC_ERR_ONGOING_REP			= 36,
	CLSIC_ERR_REPS_COMPLETE			= 37,
	CLSIC_ERR_REP_TRGR_TIMEOUT		= 38,
	CLSIC_ERR_REP_UNEXPECTED_TRGR		= 39,
	CLSIC_ERR_REP_NOISE_LVL			= 40,
	CLSIC_ERR_REP_SPEECH_RATIO		= 41,
	CLSIC_ERR_REP_NET_SPEECH		= 42,
	CLSIC_ERR_REP_SATURATION		= 43,
	CLSIC_ERR_REP_FEATURE_OVERFLOW		= 44,
	CLSIC_ERR_REPS_NOT_ENOUGH_VALID		= 45,
	CLSIC_ERR_AUTH_IN_PROGRESS		= 46,
	CLSIC_ERR_INVAL_TRGR_DOMAIN		= 47,
	CLSIC_ERR_INVAL_ASR_BLOCK_SZ		= 48,
	CLSIC_ERR_ALREADY_LISTENING		= 49,
	CLSIC_ERR_INVAL_CMD_FOR_TRGR_DOMAIN	= 50,
	CLSIC_ERR_LISTEN_NOT_STARTED		= 51,
	CLSIC_ERR_INVAL_TRGR_INFO		= 52,
	CLSIC_ERR_INVAL_TRGR_ENGINEID		= 53,
	CLSIC_ERR_INVAL_TRGR_PHRASEID		= 54,
	CLSIC_ERR_ASR_PREV_REQUEST_PENDING	= 55,
	CLSIC_ERR_ASR_STREAM			= 56,
	CLSIC_ERR_ASR_STREAM_DISABLED		= 57,
	CLSIC_ERR_KEY_NOT_FOUND			= 58,
	CLSIC_ERR_BPB_SZ_TOO_SMALL		= 59,
	CLSIC_ERR_BPB_SZ_UNALIGNED		= 60,
	CLSIC_ERR_BPB_BAD_HDR			= 61,
	CLSIC_ERR_BPB_BAD_IMGMAP		= 62,
	CLSIC_ERR_BPB_SZ_INCONSISTENT		= 63,
	CLSIC_ERR_BPB_AUTH_FAILED		= 64,
	CLSIC_ERR_COV_DISABLED			= 65,
	CLSIC_ERR_INVALID_ENROL_DURATION	= 66,
	CLSIC_ERR_INVALID_AUTH_RESULT_FORMAT	= 67,
	CLSIC_ERR_BPB_ASSET_INVAL_VER		= 68,
	CLSIC_ERR_BPB_ASSET_INVAL_SZ		= 69,
	CLSIC_ERR_BPB_ASSET_INVAL_COMP_TYPE	= 70,
	CLSIC_ERR_BPB_ASSET_INVAL_COMP_TABLE_SZ	= 71,
	CLSIC_ERR_BPB_ASSET_INVAL_FLAGS		= 72,
	CLSIC_ERR_AUTH_MAX_AUDIO_PROCESSED	= 73,
	CLSIC_ERR_AUTH_NO_USERS_TO_MATCH	= 74,
	CLSIC_ERR_AUTH_BIOM_DISABLED		= 75,
	CLSIC_ERR_REP_PLOSIVE			= 76,
	CLSIC_ERR_REP_SNR			= 77,
	CLSIC_ERR_REP_REWIND_OVF		= 78,
	CLSIC_ERR_INVALID_BIN_ID		= 79,
	CLSIC_ERR_INVALID_BIN_DATA		= 80,
	CLSIC_ERR_BIN_NOT_INSTALLED		= 81,
	CLSIC_ERR_BIOVTE_MAP_INVALID		= 82,
	CLSIC_ERR_BIOVTE_MAP_SZ_INVALID 	= 83,
	CLSIC_ERR_BIOVTE_MAP_NOT_INSTALLED	= 84,
	CLSIC_ERR_BIOVTE_MAPPING_DOES_NOT_EXIST = 85,
	CLSIC_ERR_IOCTL_EXT_CODEC		= 86,
	CLSIC_ERR_BL_AUTH_FAILED		= 200,
	CLSIC_ERR_BL_INVAL_VERSION		= 201,
	CLSIC_ERR_BL_FLASH_WRITE_FAILED		= 202,
	CLSIC_ERR_BL_ARB_CHECK_FAILED		= 203,
	CLSIC_ERR_BL_CLUB_TOO_LARGE		= 204,
	CLSIC_ERR_BL_IMG_NAME_CLASH		= 205,
	CLSIC_ERR_BL_CAB_NOT_1ST_IN_MAB		= 206,
	CLSIC_ERR_BL_TOO_MANY_IMGS		= 207,
	CLSIC_ERR_BL_NO_MIN_SET_IN_MAB		= 208,
	CLSIC_ERR_BL_FLASH_ERASE_FAILED		= 209,
	CLSIC_ERR_BL_FLASH_READ_FAILED		= 210,
	CLSIC_ERR_BL_NBS2_NOT_1ST_IN_CAB	= 211,
	CLSIC_ERR_BL_OSAPP_NOT_2ND_IN_CAB	= 212,
};

/**
 *  Header structures for fixed size messages.
 */
struct clsic_cmd_hdr {
	uint8_t sbc;
	uint8_t msgid;
} PACKED;

struct clsic_rsp_hdr {
	uint8_t sbc;
	uint8_t msgid;
	uint8_t err;
} PACKED;

struct clsic_ack_hdr {
	uint8_t sbc;
	uint8_t msgid;
} PACKED;

struct clsic_nty_hdr {
	uint8_t sbc;
	uint8_t msgid;
} PACKED;

/**
 *
 */
static inline void clsic_cmd_hdr_init(struct clsic_cmd_hdr *hdr, uint8_t msgid) {
	clsic_set_bulk(&hdr->sbc, 0);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_CMD);
	hdr->msgid = msgid;
}

static inline void clsic_rsp_hdr_init(struct clsic_rsp_hdr *hdr, uint8_t msgid,
				      uint8_t err) {
	clsic_set_bulk(&hdr->sbc, 0);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_RSP);
	hdr->msgid = msgid;
	hdr->err = err;
}

static inline void clsic_ack_hdr_init(struct clsic_ack_hdr *hdr, uint8_t msgid) {
	clsic_set_bulk(&hdr->sbc, 0);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_ACK);
	hdr->msgid = msgid;
}

static inline void clsic_nty_hdr_init(struct clsic_nty_hdr *hdr, uint8_t msgid) {
	clsic_set_bulk(&hdr->sbc, 0);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_NTY);
	hdr->msgid = msgid;
}

/**
 *  Header structures for bulk messages.
 */
struct clsic_blkcmd_hdr {
	uint8_t sbc;
	uint8_t msgid;
	uint32_t bulk_sz;
} PACKED;

struct clsic_blkrsp_hdr {
	uint8_t sbc;
	uint8_t msgid;
	uint32_t bulk_sz;
	uint8_t err;
} PACKED;

struct clsic_blknty_hdr {
	uint8_t sbc;
	uint8_t msgid;
	uint32_t bulk_sz;
} PACKED;

static inline void clsic_blkcmd_hdr_init(struct clsic_blkcmd_hdr *hdr,
					 uint8_t msgid, uint32_t bulk_sz) {
	clsic_set_bulk(&hdr->sbc, 1);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_CMD);
	hdr->msgid = msgid;
	hdr->bulk_sz = bulk_sz;
}

static inline void clsic_blkrsp_hdr_init(struct clsic_blkrsp_hdr *hdr,
					 uint8_t msgid, uint32_t bulk_sz,
					 uint8_t err) {
	clsic_set_bulk(&hdr->sbc, 1);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_RSP);
	hdr->msgid = msgid;
	hdr->err = err;
	hdr->bulk_sz = bulk_sz;
}

static inline void clsic_blknty_hdr_init(struct clsic_blknty_hdr *hdr,
					 uint8_t msgid, uint32_t bulk_sz) {
	clsic_set_bulk(&hdr->sbc, 1);
	clsic_set_cran(&hdr->sbc, CLSIC_CRAN_NTY);
	hdr->msgid = msgid;
	hdr->bulk_sz = bulk_sz;
}

/**
 *  Structure output as the bulk part of all PANIC and GET_DEBUG_INFO messages.
 *  The payload starting at salt[0] to the end of the structure should be
 *  considered opaque and is encrypted if "encrypted" is non-zero. If
 *  encrypted, "key" contains the compressed public half of the ECC256 key
 *  generated by the SP for ECDH cryptography.
 */
struct clsic_debug_info {
	uint32_t version;
	uint8_t encrypted;
	uint8_t key[33];
	uint8_t salt[4];
	uint8_t info[2044];
	uint8_t pad[2];
} PACKED;

/**
 *  System Service message identifiers.
 */
enum clsic_sys_msg_id {
	CLSIC_SYS_MSG_CR_SYS_INFO		= 0,
	CLSIC_SYS_MSG_CR_SRV_INFO		= 1,
	CLSIC_SYS_MSG_CR_SP_SHDN		= 2,
	CLSIC_SYS_MSG_N_RXDMA_STS		= 3,
	CLSIC_SYS_MSG_N_INVAL_CMD		= 4,
	CLSIC_SYS_MSG_N_PANIC			= 5,
	CLSIC_SYS_MSG_CR_SET_TRACE_FILTER	= 6,
	CLSIC_SYS_MSG_CR_GET_DEBUG_INFO		= 7,
	CLSIC_SYS_MSG_CR_GET_KEY_VAL		= 8,
	CLSIC_SYS_MSG_CR_GET_DI_CATEGORY_COUNT	= 9,
	CLSIC_SYS_MSG_CR_GET_DI_PAGE_COUNT	= 10,
	CLSIC_SYS_MSG_CR_IOCTL			= 11,
};

/**
 *  System Service ioctl id's.
 */
enum clsic_sys_ioctl {
	CLSIC_SYS_IOCTL_EXT_CODEC_COMMS_HALT	= 0,
	CLSIC_SYS_IOCTL_EXT_CODEC_COMMS_RESUME	= 1,
};

/**
 *  System Service messages.
 */
union clsic_sys_msg {
	uint8_t raw_msg[CLSIC_FIXED_MSG_SZ];

	/**
	 *  CLSIC_SYS_MSG_CR_SYS_INFO command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_sys_info;

	/**
	 *  CLSIC_SYS_MSG_CR_SYS_INFO response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint8_t srv_count;
	} PACKED rsp_sys_info;

	/**
	 *  CLSIC_SYS_MSG_CR_SRV_INFO command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t srv_inst;
	} PACKED cmd_srv_info;

	/**
	 *  CLSIC_SYS_MSG_CR_SRV_INFO response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t srv_type;
		uint32_t srv_ver;
	} PACKED rsp_srv_info;

	/**
	 *  CLSIC_SYS_MSG_CR_SP_SHDN command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_sp_shdn;

	/**
	 *  CLSIC_SYS_MSG_CR_SP_SHDN response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint32_t srvs_hold_wakelock;
	} PACKED rsp_sp_shdn;

	/**
	 *  CLSIC_SYS_MSG_N_RXDMA_STS notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t err;
		uint8_t srv_inst;
		uint8_t msgid;
		uint8_t slice_num;
		uint32_t slice_sz;
	} PACKED nty_rxdma_sts;

	/**
	 *  CLSIC_SYS_MSG_N_INVAL_CMD notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t err;
		uint8_t srv_inst;
		uint8_t msgid;
	} PACKED nty_inval_cmd;

	/**
	 *  CLSIC_SYS_MSG_N_PANIC notification structure.
	 */
	struct {
		struct clsic_blknty_hdr hdr;
	} PACKED blknty_panic;

	/**
	 *  CLSIC_SYS_MSG_CR_SET_TRACE_FILTER command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint32_t new_level;
		uint32_t new_mask;
	} PACKED cmd_set_trace_filter;

	/**
	 *  CLSIC_SYS_MSG_CR_SET_TRACE_FILTER response structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint32_t old_level;
		uint32_t old_mask;
	} PACKED rsp_set_trace_filter;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DI_CATEGORY_COUNT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_di_category_count;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DI_CATEGORY_COUNT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category_count;
	} PACKED rsp_get_di_category_count;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DI_PAGE_COUNT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint16_t category;
	} PACKED cmd_get_di_page_count;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DI_PAGE_COUNT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category;
		uint16_t page_count;
	} PACKED rsp_get_di_page_count;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DEBUG_INFO command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED cmd_get_debug_info;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DEBUG_INFO response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED rsp_get_debug_info;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_DEBUG_INFO response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED blkrsp_get_debug_info;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_KEY_VAL command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_get_key_val;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_KEY_VAL response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_get_key_val;

	/**
	 *  CLSIC_SYS_MSG_CR_GET_KEY_VAL response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_get_key_val;

	/**
	 *  CLSIC_SYS_MSG_CR_IOCTL command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint8_t id;
		union {
			uint8_t raw_payload[9];
		} PACKED args;
	} PACKED cmd_ioctl;

	/**
	 *  CLSIC_SYS_MSG_CR_IOCTL response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint8_t id;
		union {
			uint8_t raw_payload[8];
		} PACKED args;
	} PACKED rsp_ioctl;
} PACKED;

/**
 *  Maximum Size in bytes of a registry key/value.
 */
#define CLSIC_MAX_REGISTRY_KEY_SZ			(64)

/**
 *  Bulk part of the CLSIC_SYS_MSG_CR_GET_KEY_VAL command.
 *  The actual length of the key can be less than or equal to
 *  CLSIC_MAX_REGISTRY_KEY_SZ (key is not null terminated and the
 *  length is explicitly specified in the .length field).
 */
struct clsic_sys_registry_key {
	uint8_t val_idx;
	uint8_t length;
	int8_t  key[CLSIC_MAX_REGISTRY_KEY_SZ];
	uint8_t pad[2];
} PACKED;

/**
 *  Registry Tags
 */
enum clsic_registry_tags {
	CLSIC_REGISTRY_KEY    = 1,
	CLSIC_REGISTRY_INT    = 2,
	CLSIC_REGISTRY_BOOL   = 3,
	CLSIC_REGISTRY_STRING = 4,
};

/**
 *  Bulk part of the CLSIC_SYS_MSG_CR_GET_KEY_VAL response.
 *  The actual length of the key value can be less than or equal to
 *  CLSIC_MAX_REGISTRY_KEY_SZ (if key value is a string then it is not
 *  null terminated and the length is explicitly specified in the
 * .length field).
 */
struct clsic_sys_registry_value {
	uint8_t tag;
	uint8_t length;
	union {
		int8_t  str_val[CLSIC_MAX_REGISTRY_KEY_SZ];
		int32_t int_val;
		int8_t  bool_val;
	} PACKED clsic_key_val;
	uint8_t pad[2];
} PACKED;


/**
 *  Register Access Service message identifiers.
 */
enum clsic_ras_msg_id {
	CLSIC_RAS_MSG_CR_RDREG			= 1,
	CLSIC_RAS_MSG_CR_WRREG			= 2,
	CLSIC_RAS_MSG_CR_GET_DEBUG_INFO		= 3,
	CLSIC_RAS_MSG_CR_RDREG_BULK		= 4,
	CLSIC_RAS_MSG_CR_WRREG_BULK		= 5,
	CLSIC_RAS_MSG_CR_SET_IRQ_NTY_MODE	= 6,
	CLSIC_RAS_MSG_N_IRQ			= 7,
	CLSIC_RAS_MSG_CR_GET_DI_CATEGORY_COUNT	= 8,
	CLSIC_RAS_MSG_CR_GET_DI_PAGE_COUNT	= 9,
};

enum clsic_ras_irq_nty_mode {
	CLSIC_RAS_NTY_REQ		= 1,
	CLSIC_RAS_NTY_FLUSH_AND_REQ	= 2,
	CLSIC_RAS_NTY_CANCEL		= 3,
};

enum clsic_ras_irq_id {
	CLSIC_RAS_IRQ_DSP2_0		= 0,
	CLSIC_RAS_IRQ_DSP2_1		= 1,
	CLSIC_RAS_IRQ_DSP2_2		= 2,
	CLSIC_RAS_IRQ_DSP2_3		= 3,
	CLSIC_RAS_IRQ_COUNT		= 4,
};

/**
 *  Register Access Service messages.
 */
union clsic_ras_msg {
	uint8_t raw_msg[CLSIC_FIXED_MSG_SZ];

	/**
	 *  CLSIC_RAS_MSG_CR_RDREG command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint32_t addr;
	} PACKED cmd_rdreg;

	/**
	 *  CLSIC_RAS_MSG_CR_RDREG response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint32_t value;
	} PACKED rsp_rdreg;

	/**
	 *  CLSIC_RAS_MSG_CR_WRREG command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint32_t addr;
		uint32_t value;
	} PACKED cmd_wrreg;

	/**
	 *  CLSIC_RAS_MSG_CR_WRREG response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_wrreg;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DI_CATEGORY_COUNT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_di_category_count;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DI_CATEGORY_COUNT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category_count;
	} PACKED rsp_get_di_category_count;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DI_PAGE_COUNT command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint16_t category;
	} PACKED cmd_get_di_page_count;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DI_PAGE_COUNT response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category;
		uint16_t page_count;
	} PACKED rsp_get_di_page_count;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DEBUG_INFO command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED cmd_get_debug_info;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DEBUG_INFO response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED rsp_get_debug_info;

	/**
	 *  CLSIC_RAS_MSG_CR_GET_DEBUG_INFO response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
		uint16_t category;
		uint16_t page;
	} PACKED blkrsp_get_debug_info;

	/**
	 *  CLSIC_RAS_MSG_CR_RDREG_BULK command structure.
	 */
	struct
	{
		struct clsic_cmd_hdr hdr;
		uint32_t addr;
		uint32_t byte_count;
	} PACKED cmd_rdreg_bulk;

	/**
	 *  CLSIC_RAS_MSG_CR_RDREG_BULK response structure.
	 */
	struct
	{
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_rdreg_bulk;

	/**
	 *  CLSIC_RAS_MSG_CR_RDREG_BULK error response structure.
	 */
	struct
	{
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_rdreg_bulk;

	/**
	 *  CLSIC_RAS_MSG_CR_WRREG_BULK command structure.
	 */
	struct
	{
		struct clsic_blkcmd_hdr hdr;
		uint32_t addr;
	} PACKED blkcmd_wrreg_bulk;

	/**
	 *  CLSIC_RAS_MSG_CR_WRREG_BULK response structure.
	 */
	struct
	{
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_wrreg_bulk;

	/**
	 *  CLSIC_RAS_MSG_CR_SET_IRQ_NTY_MODE command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
		uint32_t irq_id;
		uint32_t mode;
	} PACKED cmd_set_irq_nty_mode;

	/**
	 *  CLSIC_RAS_MSG_CR_SET_IRQ_NTY_MODE response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_set_irq_nty_mode;

	/**
	 *  CLSIC_RAS_MSG_N_IRQ notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
        uint32_t irq_id;
	} PACKED nty_irq;
} PACKED;

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
	CLSIC_VOX_MSG_CR_GET_DEBUG_INFO		= 26,
	CLSIC_VOX_MSG_CR_GET_DI_CATEGORY_COUNT	= 28,
	CLSIC_VOX_MSG_CR_GET_DI_PAGE_COUNT	= 29,

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
	CLSIC_VOX_MSG_N_NEW_AUTH_RESULT 	= 27,

	/**
	 *  VOX Messages for Manage mode.
	 */
	CLSIC_VOX_MSG_CR_INSTALL_PHRASE		= 22,
	CLSIC_VOX_MSG_CR_IS_PHRASE_INSTALLED	= 20,
	CLSIC_VOX_MSG_CR_REMOVE_PHRASE		= 21,
	CLSIC_VOX_MSG_CR_IS_USER_INSTALLED	= 23,
	CLSIC_VOX_MSG_CR_REMOVE_USER		= 24,
	CLSIC_VOX_MSG_CR_GET_AUTH_KEY		= 25,
	CLSIC_VOX_MSG_CR_INSTALL_BIN		= 30,
	CLSIC_VOX_MSG_CR_REMOVE_BIN		= 31,
	CLSIC_VOX_MSG_CR_IS_BIN_INSTALLED	= 32,
	CLSIC_VOX_MSG_CR_INSTALL_BIOVTE_MAP	= 33,
	CLSIC_VOX_MSG_CR_REMOVE_BIOVTE_MAP	= 34,
	CLSIC_VOX_MSG_CR_IS_BIOVTE_MAP_INSTALLED	= 35,
};

/**
 *  VOX Service operating modes.
 */
enum clsic_vox_mode {
	CLSIC_VOX_MODE_IDLE			= 0,
	CLSIC_VOX_MODE_MANAGE			= 1,
	CLSIC_VOX_MODE_ENROL			= 2,
	CLSIC_VOX_MODE_LISTEN			= 4,
	CLSIC_VOX_MODE_STREAM			= 5,
};

/**
 *  VOX Service trigger detector domains.
 */
enum clsic_vox_trgr_domain {
	CLSIC_VOX_TRIG_DOMAIN_INTRNL		= 0,
	CLSIC_VOX_TRIG_DOMAIN_EXTRNL		= 1,
};

/**
 *  VOX Service authentication security levels.
 */
enum clsic_vox_security_lvl {
	CLSIC_VOX_SECURITY_LOW			= 0,
	CLSIC_VOX_SECURITY_MEDIUM		= 1,
	CLSIC_VOX_SECURITY_HIGH			= 2,
	CLSIC_VOX_SECURITY_LVL_COUNT		= 3,
};

/**
 *  VOX Service user identifiers.
 */
enum clsic_vox_userid {
	CLSIC_VOX_USER1				= 0,
	CLSIC_VOX_USER2				= 1,
	CLSIC_VOX_USER3				= 2,
	CLSIC_VOX_NO_USER			= 255,
};

/**
 *  VOX Service phrase identifiers.
 */
enum clsic_vox_phraseid {
	CLSIC_VOX_PHRASE_VDT1			= 0,
	CLSIC_VOX_PHRASE_TI			= 4,
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
	CLSIC_VOX_USER_FLAG_COMBINED		= 0x80,

	/**
	 *  Flag that can be optionally or'ed into the phraseid field of
	 *  phrase[0] or phrase[1] of the CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN
	 *  command. When specified, tells the implementation that the
	 *  enrolment data generated for that phrase will be discarded. This is
	 *  useful when wanting to enable the combined enrolment UX, but when
	 *  one of the phrases is already enrolled.
	 */
	CLSIC_VOX_PHRASE_FLAG_DISCARD		= 0x80,
};

/**
 *  VOX Service ASR block size in frames.
 */
enum clsic_vox_asr_blk_sz {
	CLSIC_VOX_ASR_BLK_SZ_0			= 0,
	CLSIC_VOX_ASR_BLK_SZ_192		= 1,
	CLSIC_VOX_ASR_BLK_SZ_384		= 2,
	CLSIC_VOX_ASR_BLK_SZ_960		= 3,
	CLSIC_VOX_ASR_BLK_SZ_1920		= 4,
	CLSIC_VOX_ASR_BLK_SZ_4800		= 5,
	CLSIC_VOX_ASR_BLK_SZ_9600		= 6,
};

/**
 *  VOX Service maximum num of authentication results.
 */
enum clsic_vox_max_auth_result_count {
	CLSIC_VOX_MAX_AUTH_RESULT_COUNT	= 10,
};

/**
 *  VOX Service flags that determine the format of the authentication
 *  result.
 */
enum clsic_vox_auth_result_format {
	/**
	 *  If this Flag is used then result will be in the foramt
	 *  specified by struct clsic_vox_auth_result
	 */
	CLSIC_VOX_AUTH_RESULT_CLASSIC		= 0x0,

	/**
	 *  If this Flag is used then result will be in the foramt
	 *  specified by struct clsic_vox_auth_result_ex
	 */
	CLSIC_VOX_AUTH_RESULT_EXTENDED		= 0x1,

	/**
	 *  If this Flag is used then result will be in the foramt
	 *  specified by struct clsic_vox_auth_result_ex2
	 */
	CLSIC_VOX_AUTH_RESULT_EXTENDED2		= 0x2,
};


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
		struct clsic_cmd_hdr hdr;
		uint8_t userid;
		struct {
			uint8_t phraseid;
			uint8_t rep_count;
			union {
				uint16_t duration_ms;
				uint16_t timeout_ms;
			} PACKED;
		} PACKED phrase[2];
	} PACKED cmd_install_user_begin;

	/**
	 *  CLSIC_VOX_MSG_CR_INSTALL_USER_BEGIN response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_install_user_begin;

	/**
	 *  CLSIC_VOX_MSG_CR_REP_START command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_rep_start;

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
		uint8_t	 phraseid;
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
	 *  CLSIC_VOX_MSG_CR_GET_AUTH_KEY command structure.
	 */
	struct {
		struct clsic_cmd_hdr hdr;
	} PACKED cmd_get_auth_key;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_AUTH_KEY response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_get_auth_key;

	/**
	 *  CLSIC_VOX_MSG_CR_GET_AUTH_KEY response structure.
	 */
	struct {
		struct clsic_blkrsp_hdr hdr;
	} PACKED blkrsp_get_auth_key;

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
 *  Bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER response when
 *  blkcmd_auth_user.result_format is CLSIC_VOX_AUTH_RESULT_CLASSIC
 *
 *  If result_count is greater than CLSIC_VOX_MAX_AUTH_RESULT_COUNT
 *  then only the last CLSIC_VOX_MAX_AUTH_RESULT_COUNT will be available
 *  in the below structure else result_count number of results will be
 *  available.
 *
 *  Results are sorted in ascending order of time i.e. start_frame[i+1]
 *  will be greater than end_frame[i].
 *
 *  The "secure_audio_src" field is a bit field where each of bits [0:9] are
 *  used to represent the security of each result segment. 0 means that the
 *  audio for the segment was sourced from a non-secure audio source. 1 means
 *  that the audio for the segment was sourced from a secure audio source.
 */
struct clsic_vox_auth_result {
	uint8_t nonce[16];
	uint8_t security_lvl;
	int32_t result_count;
	int32_t start_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	int32_t end_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	uint8_t sha[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][32];
	uint8_t userid[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	float score[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	uint16_t secure_audio_src;
	uint8_t pad1[7];
	uint8_t signature[74];
	uint8_t pad2[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER response when
 *  blkcmd_auth_user.result_format is CLSIC_VOX_AUTH_RESULT_EXTENDED
 *
 *  If result_count is greater than CLSIC_VOX_MAX_AUTH_RESULT_COUNT
 *  then only the last CLSIC_VOX_MAX_AUTH_RESULT_COUNT will be available
 *  in the below structure else result_count number of results will be
 *  available.
 *
 *  Results are sorted in ascending order of time i.e. start_frame[i+1]
 *  will be greater than end_frame[i].
 *
 *  The "secure_audio_src" field is a bit field where each of bits [0:9] are
 *  used to represent the security of each result segment. 0 means that the
 *  audio for the segment was sourced from a non-secure audio source. 1 means
 *  that the audiofor the segment was sourced from a secure audio source.
 *
 *  In the extended result format score will be included for all installed
 *  users even if no user was identified
 */
struct clsic_vox_auth_result_ex {
	uint8_t nonce[16];
	uint8_t security_lvl;
	int32_t result_count;
	int32_t start_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	int32_t end_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	uint8_t sha[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][32];
	uint8_t userid[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	float score[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint16_t secure_audio_src;
	uint8_t pad1[23];
	uint8_t signature[74];
	uint8_t pad2[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_AUTH_USER response when
 *  blkcmd_auth_user.result_format is CLSIC_VOX_AUTH_RESULT_EXTENDED2
 *
 *  If result_count is greater than CLSIC_VOX_MAX_AUTH_RESULT_COUNT
 *  then only the last CLSIC_VOX_MAX_AUTH_RESULT_COUNT will be available
 *  in the below structure else result_count number of results will be
 *  available.
 *
 *  Results are sorted in ascending order of time i.e. start_frame[i+1]
 *  will be greater than end_frame[i].
 *
 *  The "secure_audio_src" field is a bit field where each of bits [0:9] are
 *  used to represent the security of each result segment. 0 means that the
 *  audio for the segment was sourced from a non-secure audio source. 1 means
 *  that the audio for the segment was sourced from a secure audio source.
 *
 *  In the extended2 result format biometric scores and antispoofing scores
 *  will be included for all installed users even if no user was identified.
 *
 *  .userid would give the identified user if any, by only considering the
 *  biometric score given by .score
 *
 *  If .is_spoof is set that would mean that user identified in .userid may
 *  be because of spoof or recorded audio rather than real live audio spoken
 *  by a person
 *
 */
struct clsic_vox_auth_result_ex2 {
	uint8_t nonce[16];
	uint8_t security_lvl;
	int32_t result_count;
	int32_t start_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	int32_t end_frame[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	uint8_t sha[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][32];
	uint8_t userid[CLSIC_VOX_MAX_AUTH_RESULT_COUNT];
	float score[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint8_t is_spoof[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint8_t as_result1[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	float as_score1[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint8_t as_result2[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	float as_score2[CLSIC_VOX_MAX_AUTH_RESULT_COUNT][3];
	uint16_t secure_audio_src;
	uint8_t pad1[13];
	uint8_t signature[74];
	uint8_t pad2[2];
} PACKED;

/**
 *  Bulk part of the CLSIC_VOX_MSG_CR_GET_AUTH_KEY response.
 */
struct clsic_vox_auth_key {
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
 *  Boot Loader Service message identifiers.
 */
enum clsic_bl_msg_id {
	CLSIC_BL_MSG_CR_SET_FWU			= 33,
	CLSIC_BL_MSG_CR_SET_CPK			= 34,
	CLSIC_BL_MSG_CR_SET_MAB			= 35,
	CLSIC_BL_MSG_N_REQ_FWU			= 65,
	CLSIC_BL_MSG_N_REQ_CPK			= 66,
	CLSIC_BL_MSG_N_REQ_MAB			= 67,
	CLSIC_BL_MSG_N_NO_BOOTABLE_COMP		= 69,
	CLSIC_BL_MSG_N_FAILED_FLASH_AUTH	= 70,
	CLSIC_BL_MSG_N_FLASH_CORRUPTED		= 71,
};

/**
 *  Boot Loader component identifiers for notifications.
 */
enum clsic_bl_nty {
	CLSIC_BL_FAILED_AUTH_NBS2		= 0x10,
	CLSIC_BL_FAILED_AUTH_OSAPP		= 0x11,
	CLSIC_BL_NO_NBS2			= 0x20,
	CLSIC_BL_NO_OSAPP			= 0x21,
	CLSIC_BL_FLASH_CORRUPT_PRE_NBS2		= 0x30,
	CLSIC_BL_FLASH_CORRUPT_PRE_OSAPP	= 0x31,
	CLSIC_BL_FLASH_CORRUPT_FWU		= 0x32,
};

/**
*  Boot Loader CLSIC_BL_MSG_CR_SET_MAB flags.
*/
enum clsic_bl_flags {
	CLSIC_BL_RESET_NOT_REQUIRED		= 0x80,
};

/**
 *  Boot Loader Service messages.
 */
union clsic_bl_msg {
	uint8_t raw_msg[CLSIC_FIXED_MSG_SZ];

	/**
	 *  CLSIC_BL_MSG_CR_SET_FWU command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_set_fwu;

	/**
	 *  CLSIC_BL_MSG_CR_SET_FWU response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_set_fwu;

	/**
	 *  CLSIC_BL_MSG_CR_SET_CPK command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_set_cpk;

	/**
	 *  CLSIC_BL_MSG_CR_SET_CPK response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
	} PACKED rsp_set_cpk;

	/**
	 *  CLSIC_BL_MSG_CR_SET_MAB command structure.
	 */
	struct {
		struct clsic_blkcmd_hdr hdr;
	} PACKED blkcmd_set_mab;

	/**
	 *  CLSIC_BL_MSG_CR_SET_MAB response structure.
	 */
	struct {
		struct clsic_rsp_hdr hdr;
		uint8_t flags;
	} PACKED rsp_set_mab;

	/**
	 *  CLSIC_BL_MSG_N_REQ_FWU notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
	} PACKED nty_req_fwu;

	/**
	 *  CLSIC_BL_MSG_N_REQ_CPK notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
	} PACKED nty_req_cpk;

	/**
	 *  CLSIC_BL_MSG_N_REQ_MAB notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
	} PACKED nty_req_mab;

	/**
	 *  CLSIC_BL_MSG_N_NO_BOOTABLE_COMP notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t component;
	} PACKED nty_no_bootable_comp;

	/**
	 *  CLSIC_BL_MSG_N_FAILED_FLASH_AUTH notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t component;
	} PACKED nty_failed_flash_auth;

	/**
	 *  CLSIC_BL_MSG_N_FLASH_CORRUPTED notification structure.
	 */
	struct {
		struct clsic_nty_hdr hdr;
		uint8_t component;
	} PACKED nty_flash_corrupted;
} PACKED;

#endif
