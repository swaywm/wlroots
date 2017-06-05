#ifndef _WLR_INTERNAL_COMMON_LOG_H
#define _WLR_INTERNAL_COMMON_LOG_H
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <wlr/common/log.h>

#ifdef __GNUC__
#define ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define ATTRIB_PRINTF(start, end)
#endif

void _wlr_log(log_importance_t verbosity, const char *format, ...) ATTRIB_PRINTF(2, 3);
void _wlr_vlog(log_importance_t verbosity, const char *format, va_list args) ATTRIB_PRINTF(2, 0);

// Returns a filename relative to wlroots source directory
static inline const char *_relpath(const char *file) {
	const char *prefix = WLR_SRC_DIR;
	if (strncmp(file, prefix, strlen(prefix)) == 0) {
		file += strlen(prefix) + 1;
	}
	return file;
}

#define wlr_log(verb, fmt, ...) \
	_wlr_log(verb, "[%s:%d] " fmt, _relpath(__FILE__), __LINE__, ##__VA_ARGS__)

#define wlr_vlog(verb, fmt, args) \
	_wlr_vlog(verb, "[%s:%d] " fmt, _relpath(__FILE__), __LINE__, args)

#define wlr_log_errno(verb, fmt, ...) \
	wlr_log(verb, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif
