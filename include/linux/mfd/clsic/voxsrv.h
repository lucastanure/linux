/*
 * voxsrv.h -- CLSIC Voice Service
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Ralph Clark <ralph.clark@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CIRRUS_CLSIC_VOXSRV_H
#define CIRRUS_CLSIC_VOXSRV_H

#include <sound/compress_driver.h>

#ifdef __KERNEL__
int clsic_vox_service_start(struct clsic *clsic, struct clsic_service *handler);
#endif

/* All FIFO data must be a multiple of this. */
#define CLSIC_FIFO_ROUND		4

/* ASR stream info that will eventually be dynamically obtained from CLSIC. */
#define CLSIC_ASR_BITS_PER_SAMPLE	16
#define CLSIC_ASR_SAMPLE_RATE		16000
#define CLSIC_ASR_NUM_CHANNELS		4
#define CLSIC_MAX_ASR_BLOCK_SIZE	((9600 * CLSIC_ASR_BITS_PER_SAMPLE *\
					  CLSIC_ASR_NUM_CHANNELS) / 8)

/*
 * Timeout in ms while listening for a trigger in listen mode. Normally CLSIC
 * will wait indefinitely, but it seems sensible to define a worst case upper
 * limit.
 */
#define CLSIC_LISTENING_TIMEOUT		20000

/* An extra period to wait beyond the specified rep timeout time. */
#define CLSIC_TIMEOUT_SAFEGUARD_MS	5000

enum vox_driver_commands_return_codes {
	/* Commands. */
	clsic_vox_cmd_set_loudspeaker_state,
	clsic_vox_cmd_is_user_installed,
	clsic_vox_cmd_remove_user,
	clsic_vox_cmd_is_phrase_installed,
	clsic_vox_cmd_install_phrase,
	clsic_vox_cmd_remove_phrase,
	clsic_vox_cmd_are_bio_assets_valid,
	clsic_vox_cmd_enrol_start,
	clsic_vox_cmd_enrol_rep,
	clsic_vox_cmd_start_listen,
	clsic_vox_cmd_stop_listen,
	clsic_vox_cmd_ap_triggered,
	clsic_vox_cmd_wait_for_trigger,
	clsic_vox_cmd_get_trigger_info,
	clsic_vox_cmd_get_asr_block,
	clsic_vox_cmd_get_available_asr_data,
	clsic_vox_cmd_voice_id_challenge,
	clsic_vox_cmd_authenticate_user,
	clsic_vox_cmd_bio_result_key,
	/* Command return codes. */
	clsic_vox_cmd_failure,
	clsic_vox_cmd_success,
	clsic_vox_cmd_unhandled,
	clsic_vox_cmd_success_bulk,
};

#define CLSIC_VOX_CMD_SLOT_SIZE	sizeof(uint8_t)

#define CLSIC_MAX_ASSETS_NAME_LEN	128
#define CLSIC_ASSETS_WORKER_NAME	"clsic_bio_worker"
#define CLSIC_TOTAL_ASSETS		5
#define CLSIC_NO_ASSETS_FILENAME	"<Assets filename not set>\n"

enum vox_state_enum {
	vox_idle_state,
	vox_bio_assets_invalid_state,
	vox_enrol_config_state,
	vox_pre_ap_trig_state,
	vox_listening_state,
};

enum clsic_vox_loudspeaker {
	vox_loudspeaker_disabled,
	vox_loudspeaker_enabled,
};

#ifndef PACKED
#define PACKED __packed
#endif

/* Structures to pass between userspace and kernelspace. */
struct clsic_vox_basic_cmd_in {
	uint8_t command_id;
} PACKED;

struct clsic_vox_basic_cmd_out {
	uint8_t return_code;
	uint8_t error_code;
} PACKED;

struct clsic_set_loudspeaker_state_in {
	uint8_t command_id;
	uint8_t loudspeaker_enabled;
} PACKED;

struct clsic_is_user_installed_in {
	uint8_t command_id;
	uint8_t user_idx;
	uint8_t phrase_idx;
} PACKED;

struct clsic_is_user_installed_out {
	uint8_t return_code;
	uint8_t error_code;
	bool user_is_installed;
} PACKED;

struct clsic_remove_user_in {
	uint8_t command_id;
	uint8_t user_idx;
	uint8_t phrase_idx;
} PACKED;

struct clsic_is_phrase_installed_in {
	uint8_t command_id;
	uint8_t phrase_idx;
} PACKED;

struct clsic_is_phrase_installed_out {
	uint8_t return_code;
	uint8_t error_code;
	bool phrase_is_installed;
} PACKED;

struct clsic_install_phrase_in {
	uint8_t command_id;
	uint8_t phrase_idx;
} PACKED;

struct clsic_remove_phrase_in {
	uint8_t command_id;
	uint8_t phrase_idx;
} PACKED;

struct clsic_are_bio_assets_valid_out {
	uint8_t return_code;
	uint8_t error_code;
	bool assets_are_valid;
} PACKED;

struct clsic_enrol_start_in {
	uint8_t command_id;
	uint8_t user_idx;
	uint8_t phrase_idx;
	uint8_t num_of_rep;
	uint16_t timeout_ms;
} PACKED;

struct clsic_start_listen_in {
	uint8_t command_id;
	uint8_t trigger_domain;
	uint8_t asr_block_size;
} PACKED;

struct clsic_ap_triggered_in {
	uint8_t command_id;
	struct clsic_vox_trgr_info info;
} PACKED;

struct clsic_get_trigger_info_out {
	uint8_t return_code;
	uint8_t error_code;
	struct clsic_vox_trgr_info info;
} PACKED;

struct clsic_get_asr_block_out {
	uint8_t return_code;
	uint8_t error_code;
	uint8_t buffer_start[CLSIC_MAX_ASR_BLOCK_SIZE];
} PACKED;

struct clsic_get_available_asr_data_out {
	uint8_t return_code;
	uint8_t error_code;
	uint32_t avail_asr_data;
} PACKED;

struct clsic_voice_id_challenge_in {
	uint8_t command_id;
	uint8_t voiceid_security_lvl;
	struct clsic_vox_auth_challenge challenge;
} PACKED;

struct clsic_voice_id_challenge_out {
	uint8_t return_code;
	uint8_t error_code;
	struct clsic_vox_auth_result result;
} PACKED;

struct clsic_authenticate_user_in {
	uint8_t command_id;
	uint8_t voiceid_security_lvl;
	uint16_t duration_ms;
} PACKED;

struct clsic_bio_result_key_out {
	uint8_t return_code;
	uint8_t error_code;
	struct clsic_vox_auth_key key;
} PACKED;

#define CLSIC_MAX_RSP_SIZE	sizeof(struct clsic_get_asr_block_out)

#endif
