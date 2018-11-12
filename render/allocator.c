#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wlr/render/allocator.h>
#include <wlr/render/allocator/interface.h>

void wlr_allocator_init(struct wlr_allocator *alloc,
		const struct wlr_allocator_impl *impl) {
	assert(alloc && impl);
	assert(impl->allocate && impl->deallocate);

	alloc->impl = impl;
}

struct wlr_image *wlr_allocator_allocate(struct wlr_allocator *alloc,
	struct wlr_backend *backend, uint32_t width, uint32_t height,
	uint32_t format, size_t num_modifiers, const uint64_t *modifiers) {
	return alloc->impl->allocate(alloc, backend, width, height, format,
		num_modifiers, modifiers);
}

void wlr_allocator_deallocate(struct wlr_allocator *alloc, struct wlr_image *img) {
	alloc->impl->deallocate(alloc, img);
}
