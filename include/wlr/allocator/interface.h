/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_ALLOCATOR_INTERFACE_H
#define WLR_ALLOCATOR_INTERFACE_H

#include <stdint.h>

struct wlr_allocator;
struct wlr_drm_format;

struct wlr_allocator_interface {
	struct wlr_buffer *(*create_buffer)(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format);
	void (*destroy)(struct wlr_allocator *alloc);
};

void wlr_allocator_init(struct wlr_allocator *alloc,
	const struct wlr_allocator_interface *impl, uint32_t buffer_caps);

#endif
