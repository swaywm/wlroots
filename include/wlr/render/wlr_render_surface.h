/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_RENDER_SURFACE_H
#define WLR_TYPES_WLR_RENDER_SURFACE_H

#include <stdbool.h>
#include <pixman.h>

struct wlr_render_surface {
	const struct wlr_render_surface_impl *impl;
};

void wlr_render_surface_destroy(struct wlr_render_surface *surface);
void wlr_render_surface_resize(struct wlr_render_surface *surface,
		uint32_t width, uint32_t height);
bool wlr_render_surface_swap_buffers(struct wlr_render_surface *surface,
		pixman_region32_t *damage);

#endif
