#ifndef TYPES_WLR_BUFFER
#define TYPES_WLR_BUFFER

#include <wlr/types/wlr_buffer.h>

/**
 * Buffer capabilities.
 *
 * These bits indicate the features supported by a wlr_buffer. There is one bit
 * per function in wlr_buffer_impl.
 */
enum wlr_buffer_cap {
	WLR_BUFFER_CAP_DATA_PTR = 1 << 0,
	WLR_BUFFER_CAP_DMABUF = 1 << 1,
	WLR_BUFFER_CAP_SHM = 1 << 2,
};

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
