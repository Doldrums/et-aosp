#define CREATE_TRACE_POINTS
#include <trace/events/et.h>

void do_trace_setpte(unsigned long cycles) {
	trace_setpte(cycles);
}

#ifdef CONFIG_ARM64_ELASTIC_TRANSLATIONS
void do_trace_etflush(unsigned long cycles) {
	trace_etflush(cycles);
}

void do_trace_etset(unsigned long cycles) {
	trace_etset(cycles);
}
#endif /* CONFIG_ARM64_ELASTIC_TRANSLATIONS */
