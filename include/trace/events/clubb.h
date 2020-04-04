/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_CLUBB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CLUBB_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM clubb


TRACE_EVENT(clubb,
	TP_PROTO(const char *func),
	TP_ARGS(func),
	TP_STRUCT__entry(
		__field(const char *, func)
	),
	TP_fast_assign(
		__entry->func = func;
	),
	TP_printk("%s",__entry->func)
);

TRACE_EVENT(clubb_0,
	TP_PROTO(const char *func,
		 const char *str1
	),
	TP_ARGS(func, str1),
	TP_STRUCT__entry(
		__field(const char *, func)
		__field(const char *, str1)
	),
	TP_fast_assign(
		__entry->func = func;
		__entry->str1 = str1;
	),
	TP_printk("%s %s",__entry->func, __entry->str1)
);


TRACE_EVENT(clubb_1,
	TP_PROTO(const char *func,
		 const char *str1,
		 unsigned long int1
	),
	TP_ARGS(func, str1, int1),
	TP_STRUCT__entry(
		__field(const char *, func)
		__field(const char *, str1)
		__field(unsigned long, int1)
	),
	TP_fast_assign(
		__entry->func = func;
		__entry->str1 = str1;
		__entry->int1 = int1;
	),
	TP_printk("%s %s %lu",__entry->func, __entry->str1, __entry->int1)
);

TRACE_EVENT(clubb_2,
	TP_PROTO(const char *func,
		 const char *str1,
		 unsigned long int1,
		 const char *str2,
		 unsigned long int2
	),
	TP_ARGS(func, str1, int1, str2, int2),
	TP_STRUCT__entry(
		__field(const char *, func)
		__field(const char *, str1)
		__field(unsigned long, int1)
		__field(const char *, str2)
		__field(unsigned long, int2)
	),
	TP_fast_assign(
		__entry->func = func;
		__entry->str1 = str1;
		__entry->int1 = int1;
		__entry->str2 = str2;
		__entry->int2 = int2;
	),
	TP_printk("%s %s %lu %s %lu",__entry->func, __entry->str1, __entry->int1, __entry->str2, __entry->int2)
);

TRACE_EVENT(clubb_3,
	TP_PROTO(const char *func,
		 const char *str1,
		 unsigned long int1,
		 const char *str2,
		 unsigned long int2,
		 const char *str3,
		 unsigned long int3
	),
	TP_ARGS(func, str1, int1, str2, int2, str3, int3),
	TP_STRUCT__entry(
		__field(const char *, func)
		__field(const char *, str1)
		__field(unsigned long, int1)
		__field(const char *, str2)
		__field(unsigned long, int2)
		__field(const char *, str3)
		__field(unsigned long, int3)
	),
	TP_fast_assign(
		__entry->func = func;
		__entry->str1 = str1;
		__entry->int1 = int1;
		__entry->str2 = str2;
		__entry->int2 = int2;
		__entry->str3 = str3;
		__entry->int3 = int3;
	),
	TP_printk("%s %s %lu %s %lu %s %lu",__entry->func, __entry->str1, __entry->int1, __entry->str2, __entry->int2,  __entry->str3, __entry->int3)
);

#endif /* _TRACE_CLUBB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
