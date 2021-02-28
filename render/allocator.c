#include <assert.h>
#include <stdlib.h>
#include "render/allocator.h"

void wlr_allocator_init(struct wlr_allocator *alloc,
		const struct wlr_allocator_interface *impl) {
	assert(impl && impl->destroy && impl->create_buffer);
	alloc->impl = impl;
	wl_signal_init(&alloc->events.destroy);
}

void wlr_allocator_destroy(struct wlr_allocator *alloc) {
	if (alloc == NULL) {
		return;
	}
	wl_signal_emit(&alloc->events.destroy, NULL);
	alloc->impl->destroy(alloc);
}

struct wlr_buffer *wlr_allocator_create_buffer(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format, void *data) {
	return alloc->impl->create_buffer(alloc, width, height, format, data);
}
