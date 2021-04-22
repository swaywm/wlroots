#ifndef RENDER_SHM_ALLOCATOR_H
#define RENDER_SHM_ALLOCATOR_H

#include <wlr/types/wlr_buffer.h>
#include "render/allocator.h"

struct wlr_shm_buffer {
	struct wlr_buffer base;
	struct wlr_shm_attributes shm;
	void *data;
	size_t size;
};

struct wlr_shm_allocator {
	struct wlr_allocator base;
};

/**
 * Creates a new shared memory allocator.
 */
struct wlr_allocator *wlr_shm_allocator_create(void);

#endif
