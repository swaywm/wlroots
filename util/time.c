#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>

#include "util/time.h"

int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

uint32_t get_current_time_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return timespec_to_msec(&now);
}
