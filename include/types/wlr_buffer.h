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
 * Get a pointer to a region of memory referring to the buffer's underlying
 * storage. The format and stride can be used to interpret the memory region
 * contents.
 *
 * The returned pointer should be pointing to a valid memory region for read
 * and write operations. The returned pointer is only valid up to the next
 * buffer_end_data_ptr_access call.
 */
bool buffer_begin_data_ptr_access(struct wlr_buffer *buffer, void **data,
	uint32_t *format, size_t *stride);
void buffer_end_data_ptr_access(struct wlr_buffer *buffer);

#endif
