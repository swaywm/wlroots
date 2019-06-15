#ifndef UTIL_ARRAY_H
#define UTIL_ARRAY_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

size_t push_zeroes_to_end(uint32_t arr[], size_t n);

/**
 * Add `target` to `values` if it doesn't exist
 * "set"s should only be modified with set_* functions
 * Values MUST be greater than 0
 */
bool set_add(uint32_t values[], size_t *len, size_t cap, uint32_t target);

/**
 * Remove `target` from `values` if it exists
 * "set"s should only be modified with set_* functions
 * Values MUST be greater than 0
 */
bool set_remove(uint32_t values[], size_t *len, size_t cap, uint32_t target);

#endif
