#ifndef _WLR_COMMON_LOG_H
#define _WLR_COMMON_LOG_H
#include <stdbool.h>
#include <stdarg.h>

typedef enum {
	L_SILENT = 0,
	L_ERROR = 1,
	L_INFO = 2,
	L_DEBUG = 3,
	L_LAST,
} log_importance_t;

typedef void (*log_callback_t)(log_importance_t importance, const char *fmt, va_list args);

void wlr_init_log(log_callback_t callback);
void wlr_log_stderr(log_importance_t verbosity, const char *fmt, va_list args);

#endif
