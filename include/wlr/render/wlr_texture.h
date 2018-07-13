#ifndef WLR_RENDER_WLR_TEXTURE_H
#define WLR_RENDER_WLR_TEXTURE_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/render/dmabuf.h>

struct wlr_renderer;
struct wlr_texture_impl;

struct wlr_texture {
	const struct wlr_texture_impl *impl;
};

/**
 * Create a new texture from raw pixel data. `stride` is in bytes. The returned
 * texture is mutable.
 */
struct wlr_texture *wlr_texture_from_pixels(struct wlr_renderer *renderer,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	const void *data);

/**
 * Create a new texture from a wl_drm resource. The returned texture is
 * immutable.
 */
struct wlr_texture *wlr_texture_from_wl_drm(struct wlr_renderer *renderer,
	struct wl_resource *data);

/**
 * Create a new texture from a DMA-BUF. The returned texture is immutable.
 */
struct wlr_texture *wlr_texture_from_dmabuf(struct wlr_renderer *renderer,
	struct wlr_dmabuf_attributes *attribs);

/**
 * Get the texture width and height.
 */
void wlr_texture_get_size(struct wlr_texture *texture, int *width, int *height);

/**
 * Returns true if this texture is using a fully opaque format.
 */
bool wlr_texture_is_opaque(struct wlr_texture *texture);

/**
 * Update a texture with raw pixels. The texture must be mutable.
 */
bool wlr_texture_write_pixels(struct wlr_texture *texture,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
	const void *data);

bool wlr_texture_to_dmabuf(struct wlr_texture *texture,
	struct wlr_dmabuf_attributes *attribs);

/**
 * Destroys this wlr_texture.
 */
void wlr_texture_destroy(struct wlr_texture *texture);

#endif
