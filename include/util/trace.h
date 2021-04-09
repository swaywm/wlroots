#ifndef UTIL_TRACE_H
#define UTIL_TRACE_H

#include <wlr/util/log.h>

void wlr_trace(const char *format, ...) _WLR_ATTRIB_PRINTF(1, 2);
void wlr_vtrace(const char *format, va_list args) _WLR_ATTRIB_PRINTF(1, 0);

#endif
