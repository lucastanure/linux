/*
 * clsic-trace.h -- CLSIC tracepoints header file
 *
 * Copyright 2016-2018 Cirrus Logic
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
	TP_PROTO(u8 trigger),
	TP_ARGS(trigger),
	TP_STRUCT__entry(__field(u8, trigger)),
	TP_fast_assign(
			__entry->trigger = trigger;
		),
	TP_printk("end of triggering with trigger state %d", __entry->trigger)
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
	TP_printk(
		  "codec id: %u; channels i/o %u/%u); format: %u; sample rate: %u; fragment size/count %zu/%zu; buffer size: %zu",
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

TRACE_EVENT(clsic_vox_asr_stream_data_rcv_start,
	TP_PROTO(size_t sz),
	TP_ARGS(sz),
	TP_STRUCT__entry(__field(size_t, sz)),
	TP_fast_assign(__entry->sz = sz;),
	TP_printk("receiving: %u bytes", __entry->sz)
);

TRACE_EVENT(clsic_vox_asr_stream_data_rcv_end,
	TP_PROTO(size_t sz),
	TP_ARGS(sz),
	TP_STRUCT__entry(__field(size_t, sz)),
	TP_fast_assign(__entry->sz = sz;),
	TP_printk("receiving: %u bytes", __entry->sz)
);

TRACE_EVENT(clsic_vox_asr_stream_copy_start,
	TP_PROTO(size_t count),
	TP_ARGS(count),
	TP_STRUCT__entry(__field(size_t, count)),
	TP_fast_assign(__entry->count = count;),
	TP_printk("copying: %zu bytes", __entry->count)
);

TRACE_EVENT(clsic_vox_asr_stream_copy_end,
	TP_PROTO(size_t count),
	TP_ARGS(count),
	TP_STRUCT__entry(__field(size_t, count)),
	TP_fast_assign(__entry->count = count;),
	TP_printk("copying: %zu bytes", __entry->count)
);

TRACE_EVENT(clsic_vox_set_mode,
	TP_PROTO(enum clsic_vox_mode old_mode, enum clsic_vox_mode new_mode),
	TP_ARGS(old_mode, new_mode),
	TP_STRUCT__entry(
			__field(uint8_t, old_mode)
			__field(uint8_t, new_mode)
			),
	TP_fast_assign(
			__entry->old_mode = old_mode;
			__entry->new_mode = new_mode
		),
	TP_printk("CLSIC change vox mode from %s (0x%x) to %s (0x%x)",
		  clsic_mode_to_string(__entry->old_mode),
		  __entry->old_mode,
		  clsic_mode_to_string(__entry->new_mode),
		  __entry->new_mode
		)
);

TRACE_EVENT(clsic_vox_set_idle_and_state,
	TP_PROTO(bool set_clsic_to_idle, unsigned int drv_state),
	TP_ARGS(set_clsic_to_idle, drv_state),
	TP_STRUCT__entry(
			__field(bool, set_clsic_to_idle)
			__field(unsigned int, drv_state)
			),
	TP_fast_assign(
			__entry->set_clsic_to_idle = set_clsic_to_idle;
			__entry->drv_state = drv_state
		),
	TP_printk(
		  "CLSIC will %s set to IDLE mode and driver state will be set to %d",
		  __entry->set_clsic_to_idle ? "be" : "not be",
		  __entry->drv_state
		)
);

TRACE_EVENT(clsic_vox_remove_user,
	TP_PROTO(uint8_t user_id, uint8_t phrase_id),
	TP_ARGS(user_id, phrase_id),
	TP_STRUCT__entry(
			__field(uint8_t, user_id)
			__field(uint8_t, phrase_id)
			),
	TP_fast_assign(
			__entry->user_id = user_id;
			__entry->phrase_id = phrase_id;
		),
	TP_printk("remove user %u for phrase %u",
		  __entry->user_id,
		  __entry->phrase_id
		)
);

TRACE_EVENT(clsic_vox_start_enrol_user,
	TP_PROTO(uint8_t user_id, uint8_t phrase_id, uint16_t duration,
		 uint16_t timeout, uint8_t number_of_reps),
	TP_ARGS(user_id, phrase_id, duration, timeout, number_of_reps),
	TP_STRUCT__entry(
			__field(uint8_t, user_id)
			__field(uint8_t, phrase_id)
			__field(uint16_t, duration)
			__field(uint16_t, timeout)
			__field(uint8_t, number_of_reps)
			),
	TP_fast_assign(
			__entry->user_id = user_id;
			__entry->phrase_id = phrase_id;
			__entry->duration = duration;
			__entry->timeout = timeout;
			__entry->number_of_reps = number_of_reps;
		),
	TP_printk(
		  "enrol user %u for phrase %u with duration %ums/timeout %ums and %u reps",
		  __entry->user_id,
		  __entry->phrase_id,
		  __entry->duration,
		  __entry->timeout,
		  __entry->number_of_reps
		)
);

DECLARE_EVENT_CLASS(clsic_vox_generic,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy),
	TP_STRUCT__entry(
			__field(uint8_t, dummy)
			),
	TP_fast_assign(
		),
	TP_printk("%s", " ")
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_perform_enrol_rep,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_complete_enrolment,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_get_bio_results,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_stop_bio_results,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

TRACE_EVENT(clsic_vox_new_auth_result,
	TP_PROTO(union clsic_vox_msg *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
			__field(int32_t, total_frames_processed)
			__field(int32_t, auth_stop_reason)
			__array(uint8_t, user_id_detected,
						CLSIC_VOX_SECURITY_LVL_COUNT)
			),
	TP_fast_assign(
			__entry->total_frames_processed =
				msg->nty_new_auth_result.total_frames_processed;
			__entry->auth_stop_reason =
				msg->nty_new_auth_result.auth_stop_reason;
			memcpy(__entry->user_id_detected,
			       msg->nty_new_auth_result.userid,
			       CLSIC_VOX_SECURITY_LVL_COUNT);
		),
	TP_printk(
			"total frames: %d, LOW: user %d, MEDIUM: user %d, HIGH: user %d (auth stop reason: %d)",
			__entry->total_frames_processed,
			__entry->user_id_detected[CLSIC_VOX_SECURITY_LOW],
			__entry->user_id_detected[CLSIC_VOX_SECURITY_MEDIUM],
			__entry->user_id_detected[CLSIC_VOX_SECURITY_HIGH],
			__entry->auth_stop_reason
			)
);

TRACE_EVENT(clsic_vox_install_asset,
	TP_PROTO(char *filename, int assetid),
	TP_ARGS(filename, assetid),
	TP_STRUCT__entry(
			__array(char, file, 32)
			__field(int, id)
			),
	TP_fast_assign(
			strcpy(__entry->file, filename);
			__entry->id = assetid;
		),
	TP_printk(
			"install asset file %s with CLSIC slot ID %d",
			__entry->file,
			__entry->id
			)
);

TRACE_EVENT(clsic_vox_uninstall_phrase,
	TP_PROTO(uint8_t phrase_id),
	TP_ARGS(phrase_id),
	TP_STRUCT__entry(
			__field(uint8_t, phrase_id)
			),
	TP_fast_assign(
			__entry->phrase_id = phrase_id;
		),
	TP_printk("phrase ID %d", __entry->phrase_id)
);

TRACE_EVENT(clsic_vox_uninstall_bin,
	TP_PROTO(uint8_t bin_id),
	TP_ARGS(bin_id),
	TP_STRUCT__entry(
			__field(uint8_t, bin_id)
			),
	TP_fast_assign(
			__entry->bin_id = bin_id;
		),
	TP_printk("bin ID %d", __entry->bin_id)
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_uninstall_bio_vte_map,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
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
	TP_PROTO(uint32_t address, uint32_t value, int ret, uint8_t err),
	TP_ARGS(address, value, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, value)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->value = value;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"  addr: 0x%x val: 0x%x ret: %d (err: %d)",
			__entry->address,
			__entry->value,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_ras_simpleread,
	TP_PROTO(uint32_t address, uint32_t value, int ret, uint8_t err),
	TP_ARGS(address, value, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, value)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->value = value;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"   addr: 0x%x val: 0x%x ret: %d (err: %d)",
			__entry->address,
			__entry->value,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_ras_bulkwrite,
	TP_PROTO(uint32_t address, uint32_t count, int ret, uint8_t err),
	TP_ARGS(address, count, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, count)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->count = count;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"    addr: 0x%x count: %d ret: %d (err: %d)",
			__entry->address,
			__entry->count,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_ras_bulkread,
	TP_PROTO(uint32_t address, uint32_t count, int ret, uint8_t err),
	TP_ARGS(address, count, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, count)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->count = count;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"    addr: 0x%x count: %d ret: %d (err: %d)",
			__entry->address,
			__entry->count,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_ras_pm_handler,
	TP_PROTO(int pm_event),
	TP_ARGS(pm_event),
	TP_STRUCT__entry(
			__field(int, pm_event)
			),
	TP_fast_assign(
			__entry->pm_event = pm_event;
		),
	TP_printk(
			"    pm event: %d",
			__entry->pm_event
			)
);

TRACE_EVENT(clsic_pm,
	TP_PROTO(int event),
	TP_ARGS(event),
	TP_STRUCT__entry(
			__field(int, event)
			),
	TP_fast_assign(
			__entry->event = event;
		),
	TP_printk(
			"%s (%d)",
			clsic_pm_rpm_to_string(__entry->event),
			__entry->event
			)
);

TRACE_EVENT(clsic_msgproc_shutdown_schedule,
	TP_PROTO(int ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
			__field(int, ret)
			),
	TP_fast_assign(
			__entry->ret = ret;
		),
	TP_printk(
			"ret = %d",
			__entry->ret
			)
);

TRACE_EVENT(clsic_msgproc_shutdown_cancel,
	TP_PROTO(bool sync, int ret),
	TP_ARGS(sync, ret),
	TP_STRUCT__entry(
			__field(bool, sync)
			__field(int, ret)
			),
	TP_fast_assign(
			__entry->sync = sync;
			__entry->ret = ret;
		),
	TP_printk(
			"sync: %d, ret = %d",
			__entry->sync,
			__entry->ret
			)
);

TRACE_EVENT(clsic_alg_simple_writeregister,
	TP_PROTO(uint32_t address, uint32_t value, int ret, uint8_t err),
	TP_ARGS(address, value, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, value)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->value = value;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"  addr: 0x%x val: 0x%x ret: %d (err: %d)",
			__entry->address,
			__entry->value,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_alg_simple_readregister,
	TP_PROTO(uint32_t address, uint32_t value, int ret, uint8_t err),
	TP_ARGS(address, value, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, value)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->value = value;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"   addr: 0x%x val: 0x%x ret: %d (err: %d)",
			__entry->address,
			__entry->value,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_alg_write,
	TP_PROTO(uint32_t address, uint32_t count, int ret, uint8_t err),
	TP_ARGS(address, count, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, count)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->count = count;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"    addr: 0x%x count: %d ret: %d (err: %d)",
			__entry->address,
			__entry->count,
			__entry->ret,
			__entry->err
			)
);

TRACE_EVENT(clsic_alg_read,
	TP_PROTO(uint32_t address, uint32_t count, int ret, uint8_t err),
	TP_ARGS(address, count, ret, err),
	TP_STRUCT__entry(
			__field(uint32_t, address)
			__field(uint32_t, count)
			__field(int, ret)
			__field(uint8_t, err)
			),
	TP_fast_assign(
			__entry->address = address;
			__entry->count = count;
			__entry->ret = ret;
			__entry->err = err;
		),
	TP_printk(
			"    addr: 0x%x count: %d ret: %d (err: %d)",
			__entry->address,
			__entry->count,
			__entry->ret,
			__entry->err
			)
);

DEFINE_EVENT(clsic_vox_generic, clsic_simirq_write_asserted,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

DEFINE_EVENT(clsic_vox_generic, clsic_simirq_write_deasserted,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

#endif /* CLSIC_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE clsic-trace
#include <trace/define_trace.h>
