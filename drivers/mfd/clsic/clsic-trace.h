/*
 * clsic-trace.h -- CLSIC tracepoints header file
 *
 * Copyright 2017 CirrusLogic, Inc.
 *
 * Author: Simon Trimmer <simont@opensource.cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM clsic

#if !defined(CLSIC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define CLSIC_TRACE_H

#include <linux/mfd/clsic/message.h>
#include <linux/mfd/clsic/voxsrv.h>
#include <linux/tracepoint.h>
#include <uapi/sound/compress_offload.h>

TRACE_EVENT(clsic_fifo_readmessage,
	TP_PROTO(struct clsic_message *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
			__field(void *, msg_p)
			__array(uint8_t, fsm, CLSIC_FIXED_MSG_SZ)
			__field(uint8_t, state)
			__field(uint8_t, cran)
			__field(uint8_t, bulk)
			__field(uint8_t, servinst)
			__field(uint8_t, msgid)
			),
	TP_fast_assign(
			__entry->msg_p = msg;
			memcpy(__entry->fsm, msg->fsm.raw, CLSIC_FIXED_MSG_SZ);
			__entry->state = msg->state;
			__entry->bulk = clsic_get_bulkbit(msg);
			__entry->servinst = clsic_get_servinst(msg);
			__entry->msgid = clsic_get_messageid(msg);
			__entry->cran = clsic_cran_to_char(
							 clsic_get_cran_frommsg(msg))
		),
	TP_printk(
			" Msg %p (%d): %02x %02x [%d %c %c %d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			__entry->msg_p,
			__entry->state,
			__entry->fsm[CLSIC_FSM0],
			__entry->fsm[CLSIC_FSM1],
			__entry->servinst,
			__entry->bulk ? 'b' : 'f',
			__entry->cran,
			__entry->msgid,
			__entry->fsm[CLSIC_FSM2],
			__entry->fsm[CLSIC_FSM3],
			__entry->fsm[CLSIC_FSM4],
			__entry->fsm[CLSIC_FSM5],
			__entry->fsm[CLSIC_FSM6],
			__entry->fsm[CLSIC_FSM7],
			__entry->fsm[CLSIC_FSM8],
			__entry->fsm[CLSIC_FSM9],
			__entry->fsm[CLSIC_FSM10],
			__entry->fsm[CLSIC_FSM11])
);

TRACE_EVENT(clsic_fifo_writemessage,
	TP_PROTO(struct clsic_message *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
			__field(void *, msg_p)
			__array(uint8_t, fsm, CLSIC_FIXED_MSG_SZ)
			__field(uint8_t, state)
			__field(uint8_t, cran)
			__field(uint8_t, bulk)
			__field(uint8_t, servinst)
			__field(uint8_t, msgid)
			),
	TP_fast_assign(
			__entry->msg_p = msg;
			memcpy(__entry->fsm, msg->fsm.raw, CLSIC_FIXED_MSG_SZ);
			__entry->state = msg->state;
			__entry->bulk = clsic_get_bulkbit(msg);
			__entry->servinst = clsic_get_servinst(msg);
			__entry->msgid = clsic_get_messageid(msg);
			__entry->cran = clsic_cran_to_char(
							 clsic_get_cran_frommsg(msg))
		),
	TP_printk(
			"Msg %p (%d): %02x %02x [%d %c %c %d] %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			__entry->msg_p,
			__entry->state,
			__entry->fsm[CLSIC_FSM0],
			__entry->fsm[CLSIC_FSM1],
			__entry->servinst,
			__entry->bulk ? 'b' : 'f',
			__entry->cran,
			__entry->msgid,
			__entry->fsm[CLSIC_FSM2],
			__entry->fsm[CLSIC_FSM3],
			__entry->fsm[CLSIC_FSM4],
			__entry->fsm[CLSIC_FSM5],
			__entry->fsm[CLSIC_FSM6],
			__entry->fsm[CLSIC_FSM7],
			__entry->fsm[CLSIC_FSM8],
			__entry->fsm[CLSIC_FSM9],
			__entry->fsm[CLSIC_FSM10],
			__entry->fsm[CLSIC_FSM11])
);

TRACE_EVENT(clsic_fifo_readbulk,
	TP_PROTO(uint32_t len),
	TP_ARGS(len),
	TP_STRUCT__entry(
			__field(uint32_t, len)
			),
	TP_fast_assign(
			__entry->len = len
		),
	TP_printk(
			"    Transferring bulk read %d",
			__entry->len
			)
);

TRACE_EVENT(clsic_fifo_writebulk,
	TP_PROTO(uint32_t len, uint32_t totallen),

	TP_ARGS(len, totallen),
	TP_STRUCT__entry(
			__field(uint32_t, len)
			__field(uint32_t, totallen)
			),
	TP_fast_assign(
			__entry->len = len;
			__entry->totallen = totallen
		),
	TP_printk(
			"   Transferring bulk write %d (%d remaining)",
			__entry->len,
			__entry->totallen
			)
);

TRACE_EVENT(clsic_msg_statechange,
	TP_PROTO(struct clsic_message *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
			__field(void *, msg_p)
			__field(uint8_t, state)
			__field(uint8_t, cran)
			__field(uint8_t, bulk)
			__field(uint8_t, servinst)
			__field(uint8_t, msgid)
			),
	TP_fast_assign(
			__entry->msg_p = msg;
			__entry->state = msg->state;
			__entry->bulk = clsic_get_bulkbit(msg);
			__entry->servinst = clsic_get_servinst(msg);
			__entry->msgid = clsic_get_messageid(msg);
			__entry->cran = clsic_cran_to_char(
							 clsic_get_cran_frommsg(msg))
		),
	TP_printk(
			"  Msg %p (%d): [%d %c %c %d] %s",
			__entry->msg_p,
			__entry->state,
			__entry->servinst,
			__entry->bulk ? 'b' : 'f',
			__entry->cran,
			__entry->msgid,
			clsic_message_state_to_string(__entry->state)
			)
);

TRACE_EVENT(clsic_statechange,

	TP_PROTO(enum clsic_states state_from,
		 enum clsic_states state_to),
	TP_ARGS(state_from, state_to),
	TP_STRUCT__entry(
			__field(uint8_t, state_from)
			__field(uint8_t, state_to)
			),
	TP_fast_assign(
			__entry->state_from = state_from;
			__entry->state_to = state_to
		),
	TP_printk(
			"0x%x (%s)-> 0x%x (%s)",
			__entry->state_from,
			clsic_state_to_string(__entry->state_from),
			__entry->state_to,
			clsic_state_to_string(__entry->state_to)
			)
);

TRACE_EVENT(clsic_vox_statechange,

	TP_PROTO(enum vox_state_enum state_from,
		 enum vox_state_enum state_to),
	TP_ARGS(state_from, state_to),
	TP_STRUCT__entry(
			__field(uint8_t, state_from)
			__field(uint8_t, state_to)
			),
	TP_fast_assign(
			__entry->state_from = state_from;
			__entry->state_to = state_to
		),
	TP_printk(
			"0x%x -> 0x%x",
			__entry->state_from,
			__entry->state_to
			)
);

TRACE_EVENT(clsic_vox_modechange,
	TP_PROTO(enum clsic_vox_mode mode_from,
		 enum clsic_vox_mode mode_to),
	TP_ARGS(mode_from, mode_to),
	TP_STRUCT__entry(
			__field(uint8_t, mode_from)
			__field(uint8_t, mode_to)
			),
	TP_fast_assign(
			__entry->mode_from = mode_from;
			__entry->mode_to = mode_to
		),
	TP_printk(
			"0x%x -> 0x%x",
			__entry->mode_from,
			__entry->mode_to
			)
);

TRACE_EVENT(clsic_vox_trigger_heard,
	TP_PROTO(u8 service_instance),
	TP_ARGS(service_instance),
	TP_STRUCT__entry(__field(u8, service_instance)),
	TP_fast_assign(
			__entry->service_instance = service_instance;
		),
	TP_printk("service instance: 0x%x", __entry->service_instance)
);

TRACE_EVENT(clsic_vox_asr_stream_open,
	TP_PROTO(enum snd_compr_direction dir),
	TP_ARGS(dir),
	TP_STRUCT__entry(__field(enum snd_compr_direction, dir)),
	TP_fast_assign(
			__entry->dir = dir;
		),
	TP_printk("direction: %d", __entry->dir)
);

TRACE_EVENT(clsic_vox_asr_stream_free,
	TP_PROTO(enum snd_compr_direction  dir, size_t bytes_copied),
	TP_ARGS(dir, bytes_copied),
	TP_STRUCT__entry(
			__field(enum snd_compr_direction, dir)
			__field(size_t, bytes_copied)
			),
	TP_fast_assign(
			__entry->dir = dir;
			__entry->bytes_copied = bytes_copied;
		),
	TP_printk("direction: %d; bytes copied: %zu",
		  __entry->dir,
		  __entry->bytes_copied)
);

TRACE_EVENT(clsic_vox_asr_stream_set_params,
	TP_PROTO(struct snd_compr_params *params, size_t buffer_sz),
	TP_ARGS(params, buffer_sz),
	TP_STRUCT__entry(
			__field(u32, codec_id)
			__field(u32, ch_in)
			__field(u32, ch_out)
			__field(u32, format)
			__field(u32, sample_rate)
			__field(size_t, frag_sz)
			__field(size_t, frag_n)
			__field(size_t, buffer_sz)
			),
	TP_fast_assign(
			__entry->codec_id = params->codec.id;
			__entry->ch_in = params->codec.ch_in;
			__entry->ch_out = params->codec.ch_out;
			__entry->format = params->codec.format;
			__entry->sample_rate = params->codec.sample_rate;
			__entry->frag_sz = params->buffer.fragment_size;
			__entry->frag_n = params->buffer.fragments;
			__entry->buffer_sz = buffer_sz;
		),
	TP_printk("codec id: %u; channels i/o %u/%u); format: %u; sample rate: %u; fragment size/count %zu/%zu; buffer size: %zu",
		  __entry->codec_id,
		  __entry->ch_in,
		  __entry->ch_out,
		  __entry->format,
		  __entry->sample_rate,
		  __entry->frag_sz,
		  __entry->frag_n,
		  __entry->buffer_sz)
);

TRACE_EVENT(clsic_vox_asr_stream_listen,
	TP_PROTO(enum clsic_vox_trgr_domain trg_domain),
	TP_ARGS(trg_domain),
	TP_STRUCT__entry(__field(enum clsic_vox_trgr_domain, trg_domain)),
	TP_fast_assign(__entry->trg_domain = trg_domain;),
	TP_printk("trigger domain: %d", __entry->trg_domain)
);

TRACE_EVENT(clsic_vox_asr_stream_data_start,
	TP_PROTO(size_t bytes_copied),
	TP_ARGS(bytes_copied),
	TP_STRUCT__entry(__field(size_t, bytes_copied)),
	TP_fast_assign(__entry->bytes_copied = bytes_copied;),
	TP_printk("bytes copied (should be 0): %d", __entry->bytes_copied)
);

TRACE_EVENT(clsic_vox_asr_stream_queue_read,
	TP_PROTO(size_t bytes_copied),
	TP_ARGS(bytes_copied),
	TP_STRUCT__entry(__field(size_t, bytes_copied)),
	TP_fast_assign(__entry->bytes_copied = bytes_copied;),
	TP_printk("bytes copied up to now: %zu", __entry->bytes_copied)
);

DECLARE_EVENT_CLASS(clsic_vox_asr_stream_data_rcv_tpl,
	TP_PROTO(u32 sz, size_t read_idx, size_t write_idx),
	TP_ARGS(sz, read_idx, write_idx),
	TP_STRUCT__entry(
			__field(u32, sz)
			__field(size_t, read_idx)
			__field(size_t, write_idx)
			),
	TP_fast_assign(
			__entry->sz = sz;
			__entry->read_idx = read_idx;
			__entry->write_idx = write_idx;
		),
	TP_printk("receiving: %ub; buf r/w idxs %zu/%zu",
		  __entry->sz,
		  __entry->read_idx,
		  __entry->write_idx)
);

DEFINE_EVENT(clsic_vox_asr_stream_data_rcv_tpl,
	     clsic_vox_asr_stream_data_rcv_start,
	TP_PROTO(u32 sz, size_t read_idx, size_t write_idx),
	TP_ARGS(sz, read_idx, write_idx)
);

DEFINE_EVENT(clsic_vox_asr_stream_data_rcv_tpl,
	     clsic_vox_asr_stream_data_rcv_end,
	TP_PROTO(u32 sz, size_t read_idx, size_t write_idx),
	TP_ARGS(sz, read_idx, write_idx)
);

DECLARE_EVENT_CLASS(clsic_vox_asr_stream_copy_tpl,
	TP_PROTO(size_t count, size_t read_idx, size_t write_idx),
	TP_ARGS(count, read_idx, write_idx),
	TP_STRUCT__entry(
			__field(size_t, count)
			__field(size_t, read_idx)
			__field(size_t, write_idx)
			),
	TP_fast_assign(
			__entry->count = count;
			__entry->read_idx = read_idx;
			__entry->write_idx = write_idx;
		),
	TP_printk("copying: %zub; buf r/w idxs %zu/%zu",
		  __entry->count,
		  __entry->read_idx,
		  __entry->write_idx)
);

DEFINE_EVENT(clsic_vox_asr_stream_copy_tpl, clsic_vox_asr_stream_copy_start,
	TP_PROTO(size_t count, size_t read_idx, size_t write_idx),
	TP_ARGS(count, read_idx, write_idx)
);

DEFINE_EVENT(clsic_vox_asr_stream_copy_tpl, clsic_vox_asr_stream_copy_end,
	TP_PROTO(size_t count, size_t read_idx, size_t write_idx),
	TP_ARGS(count, read_idx, write_idx)
);

TRACE_EVENT(clsic_dev_panic,
	TP_PROTO(enum clsic_states state),
	TP_ARGS(state),
	TP_STRUCT__entry(
			__field(uint8_t, state)
			),
	TP_fast_assign(
			__entry->state = state
		),
	TP_printk(
			"was in state %s (0x%x)",
			clsic_state_to_string(__entry->state),
			__entry->state
			)
);

TRACE_EVENT(clsic_ras_simplewrite,
	TP_PROTO(uint32_t address, uint32_t value, int ret),
	TP_ARGS(address, value, ret),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, value)
			__field(int, ret)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->value = value;
			__entry->ret = ret;
		),
	TP_printk(
			"  addr: 0x%x val: 0x%x ret: %d",
			__entry->address,
			__entry->value,
			__entry->ret
			)
);

TRACE_EVENT(clsic_ras_simpleread,
	TP_PROTO(uint32_t address, uint32_t value, int ret),
	TP_ARGS(address, value, ret),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, value)
			__field(int, ret)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->value = value;
			__entry->ret = ret;
		),
	TP_printk(
			"   addr: 0x%x val: 0x%x ret: %d",
			__entry->address,
			__entry->value,
			__entry->ret
			)
);

TRACE_EVENT(clsic_ras_bulkwrite,
	TP_PROTO(uint32_t address, uint32_t count, int ret),
	TP_ARGS(address, count, ret),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, count)
			__field(int, ret)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->count = count;
			__entry->ret = ret;
		),
	TP_printk(
			"    addr: 0x%x count: %d ret: %d",
			__entry->address,
			__entry->count,
			__entry->ret
			)
);

TRACE_EVENT(clsic_ras_bulkread,
	TP_PROTO(uint32_t address, uint32_t count, int ret),
	TP_ARGS(address, count, ret),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, count)
			__field(int, ret)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->count = count;
			__entry->ret = ret;
		),
	TP_printk(
			"    addr: 0x%x count: %d ret: %d",
			__entry->address,
			__entry->count,
			__entry->ret
			)
);

#endif /* CLSIC_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE clsic-trace
#include <trace/define_trace.h>
