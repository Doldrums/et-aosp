#undef TRACE_SYSTEM
#define TRACE_SYSTEM coalapaging

#if !defined(_TRACE_COALAPAGING_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COALAPAGING_H

#include <linux/tracepoint.h>

TRACE_EVENT(coala_allocpages,
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

TRACE_EVENT(coala_rmqueue,
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

TRACE_EVENT(coala_ptescan,
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

TRACE_EVENT(coala_pmdscan,
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

#endif /* _TRACE_COALAPAGING_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
