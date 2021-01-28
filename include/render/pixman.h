#ifndef RENDER_PIXMAN
#define RENDER_PIXMAN

#include "render/allocator.h"

#include <wlr/types/wlr_buffer.h>

struct wlr_pixman_buffer {
	struct wlr_buffer base;
	struct wlr_pixman_allocator *alloc;

	struct wlr_dmabuf_attributes dmabuf;
};

struct wlr_pixman_allocator {
	struct wlr_allocator base;
};

struct wlr_pixman_allocator *wlr_pixman_allocator_create(int drm_fd);

#endif
