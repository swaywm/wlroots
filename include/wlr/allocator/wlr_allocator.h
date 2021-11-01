#ifndef WLR_ALLOCATOR_H
#define WLR_ALLOCATOR_H

#include <wayland-server-core.h>

struct wlr_allocator_interface;
struct wlr_backend;
struct wlr_drm_format_set;
struct wlr_renderer;

struct wlr_allocator {
	const struct wlr_allocator_interface *impl;

	// Capabilities of the buffers created with this allocator
	uint32_t buffer_caps;

	const struct wlr_drm_format_set *render_formats;

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
 * Create a new swapchain with the given width and height. The allocator will
 * find the best suited format in provided wlr_drm_format_set
 */
struct wlr_swapchain *wlr_allocator_create_swapchain(struct wlr_allocator *alloc,
	int width, int height, const struct wlr_drm_format_set *display_formats,
	bool allow_modifiers);

#endif
