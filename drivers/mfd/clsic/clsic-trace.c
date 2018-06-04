/*
 * clsic-trace.c -- CLSIC tracepoints
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include "clsic-trace.h"

EXPORT_TRACEPOINT_SYMBOL(clsic_fifo_readmessage);
EXPORT_TRACEPOINT_SYMBOL(clsic_fifo_readbulk);
EXPORT_TRACEPOINT_SYMBOL(clsic_fifo_writemessage);
EXPORT_TRACEPOINT_SYMBOL(clsic_fifo_writebulk);
EXPORT_TRACEPOINT_SYMBOL(clsic_msg_statechange);
EXPORT_TRACEPOINT_SYMBOL(clsic_statechange);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_modechange);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_trigger_heard);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_open);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_free);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_set_params);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_data_start);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_listen);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_queue_read);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_data_rcv_start);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_data_rcv_end);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_copy_start);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_asr_stream_copy_end);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_set_mode);
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_set_idle_and_mode);
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
EXPORT_TRACEPOINT_SYMBOL(clsic_dev_panic);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_simplewrite);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_simpleread);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_bulkwrite);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_bulkread);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_pm_handler);
EXPORT_TRACEPOINT_SYMBOL(clsic_pm);
EXPORT_TRACEPOINT_SYMBOL(clsic_msgproc_shutdown_schedule);
EXPORT_TRACEPOINT_SYMBOL(clsic_msgproc_shutdown_cancel);
EXPORT_TRACEPOINT_SYMBOL(clsic_alg_simple_writeregister);
EXPORT_TRACEPOINT_SYMBOL(clsic_alg_simple_readregister);
EXPORT_TRACEPOINT_SYMBOL(clsic_alg_write);
EXPORT_TRACEPOINT_SYMBOL(clsic_alg_read);
EXPORT_TRACEPOINT_SYMBOL(clsic_simirq_write_asserted);
EXPORT_TRACEPOINT_SYMBOL(clsic_simirq_write_deasserted);

