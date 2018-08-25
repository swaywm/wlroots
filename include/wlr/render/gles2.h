/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_GLES2_H
#define WLR_RENDER_GLES2_H

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_egl;

struct wlr_renderer *wlr_gles2_renderer_create(struct wlr_backend *backend);

struct wlr_texture *wlr_gles2_texture_from_pixels(struct wlr_egl *egl,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	const void *data);
struct wlr_texture *wlr_gles2_texture_from_wl_drm(struct wlr_egl *egl,
	struct wl_resource *data);
struct wlr_texture *wlr_gles2_texture_from_dmabuf(struct wlr_egl *egl,
	struct wlr_dmabuf_attributes *attribs);

#endif
