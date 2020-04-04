/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_BCMDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BCMDMA_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcmdma


TRACE_EVENT(bcmdma,
	TP_PROTO(const char *name
	),
	TP_ARGS(name),
	TP_STRUCT__entry(
		__field(const char *, name)
	),
	TP_fast_assign(
		__entry->name = name;
	),
	TP_printk("%s",
		  __entry->name
	)
);

#endif /* _TRACE_BCMDMA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
