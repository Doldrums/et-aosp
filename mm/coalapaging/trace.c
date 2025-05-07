#define CREATE_TRACE_POINTS
#include <trace/events/coalapaging.h>

void do_trace_coala_allocpages(unsigned long cycles) {
	trace_coala_allocpages(cycles);
}

void do_trace_coala_rmqueue(unsigned long cycles) {
	trace_coala_rmqueue(cycles);
}

void do_trace_coala_ptescan(unsigned long cycles) {
	trace_coala_ptescan(cycles);
}

void do_trace_coala_pmdscan(unsigned long cycles) {
	trace_coala_pmdscan(cycles);
}
