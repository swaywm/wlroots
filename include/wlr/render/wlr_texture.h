#ifndef WLR_RENDER_WLR_TEXTURE_H
#define WLR_RENDER_WLR_TEXTURE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <wayland-server-protocol.h>

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
