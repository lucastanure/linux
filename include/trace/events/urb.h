/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_URB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_URB_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM urb


TRACE_EVENT(urb_func,
	TP_PROTO(const char *func),
	TP_ARGS(func),
	TP_STRUCT__entry(
		__field(const char *, func)
	),
	TP_fast_assign(
		__entry->func = func;
	),
	TP_printk("%s", __entry->func)
);

#endif /* _TRACE_URB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
