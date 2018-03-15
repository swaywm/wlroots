#ifndef WLR_RENDER_H
#define WLR_RENDER_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>

struct wlr_texture;
struct wlr_renderer;

void wlr_renderer_begin(struct wlr_renderer *r, struct wlr_output *output);
void wlr_renderer_end(struct wlr_renderer *r);
void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]);
/**
 * Defines a scissor box. Only pixels that lie within the scissor box can be
 * modified by drawing functions. Providing a NULL `box` disables the scissor
 * box.
 */
void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box);
/**
 * Requests a texture handle from this renderer.
 */
struct wlr_texture *wlr_render_texture_create(struct wlr_renderer *r);
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
 * Renders a solid quad in the specified color.
 */
void wlr_render_colored_quad(struct wlr_renderer *r,
	const float color[static 4], const float matrix[static 9]);
/**
 * Renders a solid ellipse in the specified color.
 */
void wlr_render_colored_ellipse(struct wlr_renderer *r,
	const float color[static 4], const float matrix[static 9]);
/**
 * Returns a list of pixel formats supported by this renderer.
 */
const enum wl_shm_format *wlr_renderer_get_formats(
	struct wlr_renderer *r, size_t *len);
/**
 * Returns true if this wl_buffer is a DRM buffer.
 */
bool wlr_renderer_buffer_is_drm(struct wlr_renderer *renderer,
	struct wl_resource *buffer);
/**
 * Reads out of pixels of the currently bound surface into data. `stride` is in
 * bytes.
 */
bool wlr_renderer_read_pixels(struct wlr_renderer *r, enum wl_shm_format fmt,
	uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, void *data);
/**
 * Checks if a format is supported.
 */
bool wlr_renderer_format_supported(struct wlr_renderer *r,
	enum wl_shm_format fmt);
/**
 * Destroys this wlr_renderer. Textures must be destroyed separately.
 */
void wlr_renderer_destroy(struct wlr_renderer *renderer);

struct wlr_texture_impl;

struct wlr_texture {
	struct wlr_texture_impl *impl;

	bool valid;
	uint32_t format;
	int width, height;
	bool inverted_y;
	struct wl_signal destroy_signal;
	struct wl_resource *resource;
};

/**
 * Copies pixels to this texture. The buffer is not accessed after this function
 * returns.
 */
bool wlr_texture_upload_pixels(struct wlr_texture *tex,
	enum wl_shm_format format, int stride, int width, int height,
	const unsigned char *pixels);
/**
 * Copies pixels to this texture. The buffer is not accessed after this function
 * returns. Under some circumstances, this function may re-upload the entire
 * buffer - therefore, the entire buffer must be valid.
 */
bool wlr_texture_update_pixels(struct wlr_texture *surf,
	enum wl_shm_format format, int stride, int x, int y,
	int width, int height, const unsigned char *pixels);
/**
 * Copies pixels from a wl_shm_buffer into this texture. The buffer is not
 * accessed after this function returns.
 */
bool wlr_texture_upload_shm(struct wlr_texture *tex, uint32_t format,
	struct wl_shm_buffer *shm);

/**
 * Attaches the contents from the given wl_drm wl_buffer resource onto the
 * texture. The wl_resource is not used after this call.
 * Will fail (return false) if the given resource is no drm buffer.
 */
bool wlr_texture_upload_drm(struct wlr_texture *tex,
	struct wl_resource *drm_buffer);

bool wlr_texture_upload_eglimage(struct wlr_texture *tex,
	EGLImageKHR image, uint32_t width, uint32_t height);

bool wlr_texture_upload_dmabuf(struct wlr_texture *tex,
	struct wl_resource *dmabuf_resource);
/**
 * Copies a rectangle of pixels from a wl_shm_buffer onto the texture. The
 * buffer is not accessed after this function returns. Under some circumstances,
 * this function may re-upload the entire buffer - therefore, the entire buffer
 * must be valid.
 */
bool wlr_texture_update_shm(struct wlr_texture *surf, uint32_t format,
	int x, int y, int width, int height, struct wl_shm_buffer *shm);
/**
 * Destroys this wlr_texture.
 */
void wlr_texture_destroy(struct wlr_texture *texture);

#endif
