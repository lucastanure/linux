/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_ENDPOINT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ENDPOINT_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM endpoint


TRACE_EVENT(endp_func,
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

#endif /* _TRACE_ENDPOINT_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
