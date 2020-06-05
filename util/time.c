#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>

#include "util/time.h"

uint32_t get_current_time_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
