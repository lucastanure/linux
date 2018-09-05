/*
 * clsic-vox-trace.c -- CLSIC VOX tracepoints
 *
 * Copyright 2016-2018 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include "clsic-vox-trace.h"

EXPORT_TRACEPOINT_SYMBOL(clsic_vox_modechange);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_trigger_heard);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_open);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_free);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_set_params);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_data_start);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_listen);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_queue_read);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_data_rcv);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_copy_start);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_copy_end);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_set_mode);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_set_idle_and_state);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_install_asset);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_uninstall_phrase);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_uninstall_bin);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_uninstall_bio_vte_map);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_remove_user);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_start_enrol_user);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_perform_enrol_rep);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_complete_enrolment);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_get_bio_results);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_stop_bio_results);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_new_auth_result);

const char *clsic_vox_mode_to_string(enum clsic_states state)
{
	switch (state) {
	case CLSIC_VOX_MODE_IDLE:
		return "IDLE";
	case CLSIC_VOX_MODE_MANAGE:
		return "MANAGE";
	case CLSIC_VOX_MODE_ENROL:
		return "ENROL";
	case CLSIC_VOX_MODE_LISTEN:
		return "LISTEN";
	case CLSIC_VOX_MODE_STREAM:
		return "STREAM";
	default:
		return "UNKNOWN";
	}
	return "UNKNOWN";
}
