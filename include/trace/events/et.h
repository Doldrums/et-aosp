#undef TRACE_SYSTEM
#define TRACE_SYSTEM et

#if !defined(_TRACE_ET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ET_H

#include <linux/tracepoint.h>

TRACE_EVENT(etflush,
	TP_PROTO(unsigned long cycles),
	TP_ARGS(cycles),

	TP_STRUCT__entry(
		__field(	unsigned long,	cycles	)
	),

	TP_fast_assign(
		__entry->cycles	= cycles;
	),

	TP_printk("cycles: %lu", __entry->cycles)
);

TRACE_EVENT(etset,
	TP_PROTO(unsigned long cycles),
	TP_ARGS(cycles),

	TP_STRUCT__entry(
		__field(	unsigned long,	cycles	)
	),

	TP_fast_assign(
		__entry->cycles	= cycles;
	),

	TP_printk("cycles: %lu", __entry->cycles)
);

TRACE_EVENT(setpte,
	TP_PROTO(unsigned long cycles),
	TP_ARGS(cycles),

	TP_STRUCT__entry(
		__field(	unsigned long,	cycles	)
	),

	TP_fast_assign(
		__entry->cycles	= cycles;
	),

	TP_printk("cycles: %lu", __entry->cycles)
);
#endif /* _TRACE_ET_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
