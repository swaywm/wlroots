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

/**
 * Returns the buffer age of the back buffer (the one that is rendered
 * the next time renderer_begin is called on this surface).
 * Returns 0 for a completely new backbuffer or a negative number when querying
 * failed/is impossible (in both cases no assumptions are possible).
 */
int wlr_render_surface_get_buffer_age(struct wlr_render_surface *surface);

/**
 * Returns a gbm_bo holding the surfaces contents.
 * In case of buffered render_surfaces that should return the front buffer.
 * Note that this is only available for render surfaces initialized
 * with a gbm_device.
 */
struct gbm_bo* wlr_render_surface_get_bo(struct wlr_render_surface* surface);

void wlr_render_surface_destroy(struct wlr_render_surface *surface);
void wlr_render_surface_resize(struct wlr_render_surface *surface,
		uint32_t width, uint32_t height);
bool wlr_render_surface_swap_buffers(struct wlr_render_surface *surface,
		pixman_region32_t *damage);

#endif
