/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_RENDER_SURFACE_H
#define WLR_TYPES_WLR_RENDER_SURFACE_H

#include <wayland-server-protocol.h>
#include <stdbool.h>
#include <pixman.h>

enum wlr_renderer_read_pixels_flags {
	WLR_RENDERER_READ_PIXELS_Y_INVERT = 1,
};

struct wlr_renderer;
struct wl_display;
struct wl_surface;

struct wlr_render_surface {
	const struct wlr_render_surface_impl *impl;
	uint32_t width;
	uint32_t height;
};

struct wlr_render_surface *wlr_render_surface_create_headless(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height);
struct wlr_render_surface *wlr_render_surface_create_gbm(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	void *gbm_device, uint32_t gbm_use_flags);
struct wlr_render_surface *wlr_render_surface_create_xcb(
	struct wlr_renderer *renderer,
	uint32_t width, uint32_t height, void *xcb_connection, uint32_t window);
struct wlr_render_surface *wlr_render_surface_create_wl(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	struct wl_display *disp, struct wl_surface *surf);

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
struct gbm_bo *wlr_render_surface_get_bo(struct wlr_render_surface *surface);

/**
 * Reads the pixels rendered sine the last swap_buffers into data.
 * `stride` is in bytes.
 * Must not be called during rendering on this render_surface.
 * If `flags` is not NULl, the caller indicates that it accepts frame flags
 * defined in `enum wlr_renderer_read_pixels_flags`.
 */
bool wlr_render_surface_read_pixels(struct wlr_render_surface *r,
	enum wl_shm_format fmt, uint32_t *flags, uint32_t stride,
	uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
	uint32_t dst_x, uint32_t dst_y, void *data);

void wlr_render_surface_destroy(struct wlr_render_surface *surface);
void wlr_render_surface_resize(struct wlr_render_surface *surface,
	uint32_t width, uint32_t height);
bool wlr_render_surface_swap_buffers(struct wlr_render_surface *surface,
	pixman_region32_t *damage);

#endif
