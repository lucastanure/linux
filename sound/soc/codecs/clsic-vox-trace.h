/*
 * clsic-vox-trace.h -- CLSIC VOX tracepoints header file
 *
 * Copyright (C) 2015-2018 Cirrus Logic, Inc. and
 *			   Cirrus Logic International Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM clsic

#if !defined(CLSIC_VOX_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define CLSIC_VOX_TRACE_H

#include <linux/mfd/clsic/core.h>
#include <linux/mfd/clsic/message.h>
#include <linux/tracepoint.h>
#include <uapi/sound/compress_offload.h>
#include "clsic-vox-msg.h"

const char *clsic_vox_mode_to_string(enum clsic_states state);

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
	TP_PROTO(u8 msgid,
		 u8 flags),
	TP_ARGS(msgid, flags),
	TP_STRUCT__entry(
			 __field(u8, msgid)
			 __field(u8, flags)
			),
	TP_fast_assign(
			__entry->msgid = msgid;
			__entry->flags = flags;
		),
	TP_printk(
		  "end of triggering with %s %d",
		  (__entry->msgid == CLSIC_VOX_MSG_N_LISTEN_ERR) ?
			"error" : "biometric TD|TI flags",
		  __entry->flags
		  )
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
	TP_printk("bytes copied (should be 0): %zu", __entry->bytes_copied)
);

TRACE_EVENT(clsic_vox_asr_stream_queue_read,
	TP_PROTO(size_t bytes_copied),
	TP_ARGS(bytes_copied),
	TP_STRUCT__entry(__field(size_t, bytes_copied)),
	TP_fast_assign(__entry->bytes_copied = bytes_copied;),
	TP_printk("bytes copied up to now: %zu", __entry->bytes_copied)
);

TRACE_EVENT(clsic_vox_asr_stream_data_rcv,
	TP_PROTO(size_t sz),
	TP_ARGS(sz),
	TP_STRUCT__entry(__field(size_t, sz)),
	TP_fast_assign(__entry->sz = sz;),
	TP_printk("receiving: %zu bytes", __entry->sz)
);

TRACE_EVENT(clsic_vox_asr_stream_copy_start,
	TP_PROTO(size_t count),
	TP_ARGS(count),
	TP_STRUCT__entry(__field(size_t, count)),
	TP_fast_assign(__entry->count = count;),
	TP_printk("copying: %zu bytes", __entry->count)
);

TRACE_EVENT(clsic_vox_asr_stream_copy_end,
	TP_PROTO(size_t count, unsigned int copied_total),
	TP_ARGS(count, copied_total),
	TP_STRUCT__entry(__field(size_t, count)
			 __field(unsigned int, copied_total)),
	TP_fast_assign(__entry->count = count;
		       __entry->copied_total = copied_total),
	TP_printk("copied: %zu bytes of total so far: %u bytes",
		  __entry->count, __entry->copied_total)
);

TRACE_EVENT(clsic_vox_asr_stream_trigger,
	TP_PROTO(int cmd),
	TP_ARGS(cmd),
	TP_STRUCT__entry(__field(int, cmd)),
	TP_fast_assign(__entry->cmd = cmd;),
	TP_printk("command %s (%d)",
		  (__entry->cmd == 1) ? "SNDRV_PCM_TRIGGER_START" :
		  "SNDRV_PCM_TRIGGER_STOP", __entry->cmd)
);

TRACE_EVENT(clsic_vox_asr_stream_pointer,
	TP_PROTO(unsigned int copied_total, unsigned int sample_rate),
	TP_ARGS(copied_total, sample_rate),
	TP_STRUCT__entry(__field(unsigned int, copied_total)
			 __field(unsigned int, sample_rate)),
	TP_fast_assign(__entry->copied_total = copied_total;
		       __entry->sample_rate = sample_rate),
	TP_printk("copied total: %d bytes sample rate: %u Hz",
		  __entry->copied_total, __entry->sample_rate)
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

DEFINE_EVENT(clsic_vox_generic, clsic_vox_asr_stream_get_caps,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
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
		  clsic_vox_mode_to_string(__entry->old_mode),
		  __entry->old_mode,
		  clsic_vox_mode_to_string(__entry->new_mode),
		  __entry->new_mode
		)
);

TRACE_EVENT(clsic_vox_set_idle_and_state,
	TP_PROTO(int set_clsic_to_idle, unsigned int drv_state),
	TP_ARGS(set_clsic_to_idle, drv_state),
	TP_STRUCT__entry(
			__field(int, set_clsic_to_idle)
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
	TP_PROTO(uint8_t user_id, uint8_t phrase_id, uint16_t timeout),
	TP_ARGS(user_id, phrase_id, timeout),
	TP_STRUCT__entry(
			__field(uint8_t, user_id)
			__field(uint8_t, phrase_id)
			__field(uint16_t, timeout)
			),
	TP_fast_assign(
			__entry->user_id = user_id;
			__entry->phrase_id = phrase_id;
			__entry->timeout = timeout;
		),
	TP_printk(
		  "enrol user %u for phrase %u with timeout %ums",
		  __entry->user_id,
		  __entry->phrase_id,
		  __entry->timeout
		)
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
			),
	TP_fast_assign(
			__entry->total_frames_processed =
				msg->nty_new_auth_result.total_frames_processed;
			__entry->auth_stop_reason =
				msg->nty_new_auth_result.auth_stop_reason;
		),
	TP_printk(
			"total frames: %d auth stop reason: %d (0x%x)",
			__entry->total_frames_processed,
			__entry->auth_stop_reason,
			__entry->auth_stop_reason
			)
);

/*
 * VOX_ASSET_TYPE_NAME_MAX_LEN is 43 - if it becomes longer then the
 * clsic_vox_install_asset event will safely truncate filenames
 */
TRACE_EVENT(clsic_vox_install_asset,
	TP_PROTO(char *filename, int assetid, unsigned int type),
	TP_ARGS(filename, assetid, type),
	TP_STRUCT__entry(
			__array(char, file, 43)
			__field(int, id)
			__field(unsigned int, type)
			),
	TP_fast_assign(
			strncpy(__entry->file, filename, 43);
			__entry->id = assetid;
			__entry->type = type;
		),
	TP_printk(
			"install asset type %u named %s into slot ID %d",
			__entry->type,
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

DEFINE_EVENT(clsic_vox_generic, clsic_vox_put_kvp_pub,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_factory_reset,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

TRACE_EVENT(clsic_vox_msgproc,
	TP_PROTO(uint8_t use_release, int refcount),
	TP_ARGS(use_release, refcount),
	TP_STRUCT__entry(
			__field(uint8_t, use_release)
			__field(int, refcount)
			),
	TP_fast_assign(
			__entry->use_release = use_release;
			__entry->refcount = refcount;
		),
	TP_printk("%s %d)",
		  __entry->use_release ? "use, initial refcount" :
					 "release, final refcount",
		  __entry->refcount
		)
);

TRACE_EVENT(clsic_vox_ratelimit_waiter,
	TP_PROTO(int active),
	TP_ARGS(active),
	TP_STRUCT__entry(
			__field(int, active)
			),
	TP_fast_assign(
			__entry->active = active;
		      ),
	TP_printk(
		  "%s",
		  __entry->active ? "begin" : "end"
		 )
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_perform_auth_user,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

DEFINE_EVENT(clsic_vox_generic, clsic_vox_prompted_auth,
	TP_PROTO(uint8_t dummy),
	TP_ARGS(dummy)
);

#endif /* CLSIC_VOX_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE clsic-vox-trace
#include <trace/define_trace.h>
