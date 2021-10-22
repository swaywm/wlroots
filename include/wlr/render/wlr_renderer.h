/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_WLR_RENDERER_H
#define WLR_RENDER_WLR_RENDERER_H

#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_texture.h>

enum wlr_renderer_read_pixels_flags {
	WLR_RENDERER_READ_PIXELS_Y_INVERT = 1,
};

struct wlr_renderer_impl;
struct wlr_drm_format_set;
struct wlr_buffer;
struct wlr_box;
struct wlr_fbox;
struct wlr_render_timeline;

struct wlr_renderer {
	const struct wlr_renderer_impl *impl;

	bool rendering;
	bool rendering_with_buffer;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend);

void wlr_renderer_begin(struct wlr_renderer *r, uint32_t width, uint32_t height);
bool wlr_renderer_begin_with_buffer(struct wlr_renderer *r,
	struct wlr_buffer *buffer);
void wlr_renderer_end(struct wlr_renderer *r);
void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]);
/**
 * Defines a scissor box. Only pixels that lie within the scissor box can be
 * modified by drawing functions. Providing a NULL `box` disables the scissor
 * box.
 */
void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box);
/**
 * Renders the requested texture.
 */
bool wlr_render_texture(struct wlr_renderer *r, struct wlr_texture *texture,
	const float projection[static 9], int x, int y, float alpha);
/**
 * Renders the requested texture using the provided matrix.
 */
bool wlr_render_texture_with_matrix(struct wlr_renderer *r,
	struct wlr_texture *texture, const float matrix[static 9], float alpha);
/**
 * Renders the requested texture using the provided matrix, after cropping it
 * to the provided rectangle.
 */
bool wlr_render_subtexture_with_matrix(struct wlr_renderer *r,
	struct wlr_texture *texture, const struct wlr_fbox *box,
	const float matrix[static 9], float alpha);
/**
 * Renders a solid rectangle in the specified color.
 */
void wlr_render_rect(struct wlr_renderer *r, const struct wlr_box *box,
	const float color[static 4], const float projection[static 9]);
/**
 * Renders a solid quadrangle in the specified color with the specified matrix.
 */
void wlr_render_quad_with_matrix(struct wlr_renderer *r,
	const float color[static 4], const float matrix[static 9]);
/**
 * Get the shared-memory formats supporting import usage. Buffers allocated
 * with a format from this list may be imported via wlr_texture_from_pixels.
 */
const uint32_t *wlr_renderer_get_shm_texture_formats(
	struct wlr_renderer *r, size_t *len);
/**
 * Get the DMA-BUF formats supporting sampling usage. Buffers allocated with
 * a format from this list may be imported via wlr_texture_from_dmabuf.
 */
const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_texture_formats(
	struct wlr_renderer *renderer);
/**
 * Reads out of pixels of the currently bound surface into data. `stride` is in
 * bytes.
 *
 * If `flags` is not NULl, the caller indicates that it accepts frame flags
 * defined in `enum wlr_renderer_read_pixels_flags`.
 */
bool wlr_renderer_read_pixels(struct wlr_renderer *r, uint32_t fmt,
	uint32_t *flags, uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, void *data);

/**
 * Creates necessary shm and invokes the initialization of the implementation.
 *
 * Returns false on failure.
 */
bool wlr_renderer_init_wl_display(struct wlr_renderer *r,
	struct wl_display *wl_display);

/**
 * Obtains the FD of the DRM device used for rendering, or -1 if unavailable.
 *
 * The caller doesn't have ownership of the FD, it must not close it.
 */
int wlr_renderer_get_drm_fd(struct wlr_renderer *r);

/**
 * Wait for a timeline synchronization point before continuing rendering
 * operations.
 *
 * The renderer implementation is allowed to wait sooner, e.g. wait before
 * executing any rendering operation since the previous wlr_renderer_begin()
 * call.
 *
 * When a compositor calls wlr_renderer_wait_timeline(), the renderer is
 * allowed to skip implicit wait synchronization. Compositors are not allowed
 * to mix implicit and explicit wait synchronization usage.
 */
bool wlr_renderer_wait_timeline(struct wlr_renderer *renderer,
	struct wlr_render_timeline *timeline, uint64_t src_point);
/**
 * Signal a timeline synchronization point when all previous rendering
 * operations have finished.
 *
 * The renderer implementation is allowed to signal later, e.g. signal when all
 * rendering operations up to wlr_renderer_end() have finished.
 *
 * When a compositor calls wlr_renderer_signal_timeline(), the renderer is
 * allowed to skip implicit signal synchronization. Compositors are not allowed
 * to mix implicit and explicit signal synchronization usage.
 */
bool wlr_renderer_signal_timeline(struct wlr_renderer *renderer,
	struct wlr_render_timeline *timeline, uint64_t dst_point);

/**
 * Destroys this wlr_renderer. Textures must be destroyed separately.
 */
void wlr_renderer_destroy(struct wlr_renderer *renderer);

#endif
