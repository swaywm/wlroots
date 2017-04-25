#define _POSIX_C_SOURCE 1
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "wlr/common/log.h"
#include "common/log.h"

static bool colored = true;
static log_callback_t log_callback;

static const char *verbosity_colors[] = {
	[L_SILENT] = "",
	[L_ERROR ] = "\x1B[1;31m",
	[L_INFO  ] = "\x1B[1;34m",
	[L_DEBUG ] = "\x1B[1;30m",
};

void wlr_log_init(log_callback_t callback) {
	log_callback = callback;
	// TODO: Use log callback
}

void _wlr_vlog(const char *filename, int line, log_importance_t verbosity,
		const char *format, va_list args) {
	// prefix the time to the log message
	static struct tm result;
	static time_t t;
	static struct tm *tm_info;
	char buffer[26];

	// get current time
	t = time(NULL);
	// convert time to local time (determined by the locale)
	tm_info = localtime_r(&t, &result);
	// generate time prefix
	strftime(buffer, sizeof(buffer), "%x %X - ", tm_info);
	fprintf(stderr, "%s", buffer);

	unsigned int c = verbosity;
	if (c > sizeof(verbosity_colors) / sizeof(char *) - 1) {
		c = sizeof(verbosity_colors) / sizeof(char *) - 1;
	}

	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s", verbosity_colors[c]);
	}

	if (filename && line) {
		const char *file = filename + strlen(filename);
		while (file != filename && *file != '/') {
			--file;
		}
		if (*file == '/') {
			++file;
		}
		fprintf(stderr, "[%s:%d] ", file, line);
	}

	vfprintf(stderr, format, args);

	if (colored && isatty(STDERR_FILENO)) {
		fprintf(stderr, "\x1B[0m");
	}
	fprintf(stderr, "\n");
}

void _wlr_log(const char *filename, int line, log_importance_t verbosity, const char* format, ...) {
	va_list args;
	va_start(args, format);
	_wlr_vlog(filename, line, verbosity, format, args);
	va_end(args);
}

void wlr_log_errno(log_importance_t verbosity, char* format, ...) {
	unsigned int c = verbosity;
	if (c > sizeof(verbosity_colors) / sizeof(char *) - 1) {
		c = sizeof(verbosity_colors) / sizeof(char *) - 1;
	}

	if (isatty(STDERR_FILENO)) {
		fprintf(stderr, "%s", verbosity_colors[c]);
	}

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, ": ");
	fprintf(stderr, "%s", strerror(errno));

	if (isatty(STDERR_FILENO)) {
		fprintf(stderr, "\x1B[0m");
	}
	fprintf(stderr, "\n");
}
