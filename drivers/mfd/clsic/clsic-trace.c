/*
 * clsic-trace.c -- CLSIC tracepoints
 *
 * Copyright 2016-2018 Cirrus Logic
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

