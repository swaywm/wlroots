#ifndef TYPES_WLR_BUFFER
#define TYPES_WLR_BUFFER

#include <wlr/types/wlr_buffer.h>

/**
 * Access a pointer to the allocated data from the underlying implementation,
 * its format and its stride.
 *
 * The returned pointer should be pointing to a valid memory location for read
 * and write operations.
 */
bool buffer_get_data_ptr(struct wlr_buffer *buffer, void **data,
	uint32_t *format, size_t *stride);

#endif
