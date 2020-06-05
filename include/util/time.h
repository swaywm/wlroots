#ifndef UTIL_TIME_H
#define UTIL_TIME_H

#include <time.h>

/**
 * Get the current time, in milliseconds.
 */
uint32_t get_current_time_msec(void);

/**
 * Convert a timespec to milliseconds.
 */
int64_t timespec_to_msec(const struct timespec *a);

#endif
