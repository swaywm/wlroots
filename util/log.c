#define _XOPEN_SOURCE 700 // for snprintf
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

static bool colored = true;
static enum wlr_log_importance log_importance = WLR_ERROR;

static const char *verbosity_colors[] = {
	[WLR_SILENT] = "",
	[WLR_ERROR ] = "\x1B[1;31m",
	[WLR_INFO  ] = "\x1B[1;34m",
	[WLR_DEBUG ] = "\x1B[1;30m",
};

static void log_stderr(enum wlr_log_importance verbosity, const char *fmt,
		va_list args) {
	if (verbosity > log_importance) {
		return;
	}
	// prefix the time to the log message
	struct tm result;
	time_t t = time(NULL);
	struct tm *tm_info = localtime_r(&t, &result);
	char buffer[26];

	// generate time prefix
	strftime(buffer, sizeof(buffer), "%F %T - ", tm_info);
	fprintf(stderr, "%s", buffer);

	unsigned c = (verbosity < WLR_LOG_IMPORTANCE_LAST) ? verbosity : WLR_LOG_IMPORTANCE_LAST - 1;

	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s", verbosity_colors[c]);
	}

	vfprintf(stderr, fmt, args);

	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "\x1B[0m");
	}
	fprintf(stderr, "\n");
}

static wlr_log_func_t log_callback = log_stderr;

static void log_wl(const char *fmt, va_list args) {
	static char wlr_fmt[1024];
	int n = snprintf(wlr_fmt, sizeof(wlr_fmt), "[wayland] %s", fmt);
	if (n > 0 && wlr_fmt[n - 1] == '\n') {
		wlr_fmt[n - 1] = '\0';
	}
	_wlr_vlog(WLR_INFO, wlr_fmt, args);
}

void wlr_log_init(enum wlr_log_importance verbosity, wlr_log_func_t callback) {
	if (verbosity < WLR_LOG_IMPORTANCE_LAST) {
		log_importance = verbosity;
	}
	if (callback) {
		log_callback = callback;
	}

	wl_log_set_handler_server(log_wl);
}

void _wlr_vlog(enum wlr_log_importance verbosity, const char *fmt, va_list args) {
	log_callback(verbosity, fmt, args);
}

void _wlr_log(enum wlr_log_importance verbosity, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	log_callback(verbosity, fmt, args);
	va_end(args);
}

enum wlr_log_importance wlr_log_get_verbosity(void) {
	return log_importance;
}
