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
EXPORT_TRACEPOINT_SYMBOL(clsic_vox_statechange);
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
EXPORT_TRACEPOINT_SYMBOL(clsic_dev_panic);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_simplewrite);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_simpleread);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_bulkwrite);
EXPORT_TRACEPOINT_SYMBOL(clsic_ras_bulkread);
