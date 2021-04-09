#include <stdio.h>
#include "util/trace.h"

static bool trace_initialized = false;
static FILE *trace_file = NULL;

void wlr_vtrace(const char *fmt, va_list args) {
	if (!trace_initialized) {
		trace_initialized = true;
		trace_file = fopen("/sys/kernel/tracing/trace_marker", "w");
		if (trace_file != NULL) {
			wlr_log(WLR_INFO, "Kernel tracing is enabled");
		}
	}

	if (trace_file == NULL) {
		return;
	}

	vfprintf(trace_file, fmt, args);
	fprintf(trace_file, "\n");
	fflush(trace_file);
}

void wlr_trace(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	wlr_vtrace(fmt, args);
	va_end(args);
}
