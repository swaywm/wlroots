/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_ALLOCATOR_H
#define WLR_ALLOCATOR_H

#include <wayland-server-core.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_drm_format;
struct wlr_renderer;

struct wlr_allocator_interface {
	struct wlr_buffer *(*create_buffer)(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format);
	void (*destroy)(struct wlr_allocator *alloc);
};

void wlr_allocator_init(struct wlr_allocator *alloc,
	const struct wlr_allocator_interface *impl, uint32_t buffer_caps);

struct wlr_allocator {
	const struct wlr_allocator_interface *impl;

	// Capabilities of the buffers created with this allocator
	uint32_t buffer_caps;

	struct {
		struct wl_signal destroy;
	} events;
};

/**
 * Creates the adequate wlr_allocator given a backend and a renderer
 */
struct wlr_allocator *wlr_allocator_autocreate(struct wlr_backend *backend,
	struct wlr_renderer *renderer);
/**
 * Destroy the allocator.
 */
void wlr_allocator_destroy(struct wlr_allocator *alloc);

/**
 * Allocate a new buffer.
 *
 * When the caller is done with it, they must unreference it by calling
 * wlr_buffer_drop.
 */
struct wlr_buffer *wlr_allocator_create_buffer(struct wlr_allocator *alloc,
	int width, int height, const struct wlr_drm_format *format);

#endif
