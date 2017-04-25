#ifndef _WLR_INTERNAL_COMMON_LOG_H
#define _WLR_INTERNAL_COMMON_LOG_H
#include <stdbool.h>
#include <wlr/common/log.h>

void wlr_log_errno(log_importance_t verbosity, char* format, ...) __attribute__((format(printf,2,3)));

void wlr_log_errno(log_importance_t verbosity, char* format, ...) __attribute__((format(printf,2,3)));

void _wlr_log(const char *filename, int line, log_importance_t verbosity, const char* format, ...) __attribute__((format(printf,4,5)));

#define wlr_log(VERBOSITY, FMT, ...) \
	_wlr_log(__FILE__, __LINE__, VERBOSITY, FMT, ##__VA_ARGS__)

#define wlr_vlog(VERBOSITY, FMT, VA_ARGS) \
    _wlr_vlog(__FILE__, __LINE__, VERBOSITY, FMT, VA_ARGS)

#endif
