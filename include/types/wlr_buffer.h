#ifndef TYPES_WLR_BUFFER
#define TYPES_WLR_BUFFER

#include <wlr/types/wlr_buffer.h>

struct wlr_shm_client_buffer {
	struct wlr_buffer base;

	uint32_t format;
	size_t stride;

	// The following fields are NULL if the client has destroyed the wl_buffer
	struct wl_resource *resource;
	struct wl_shm_buffer *shm_buffer;

	// This is used to keep the backing storage alive after the client has
	// destroyed the wl_buffer
	struct wl_shm_pool *saved_shm_pool;
	void *saved_data;

	struct wl_listener resource_destroy;
	struct wl_listener release;
};

struct wlr_shm_client_buffer *shm_client_buffer_create(
	struct wl_resource *resource);

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
