#ifndef RENDER_RENDERER_GLES_H
#define RENDER_RENDERER_GLES_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <pixman.h>
#include <wayland-server.h>

#include <wlr/render/allocator/gbm.h>
#include <wlr/render/egl.h>
#include <wlr/render/format_set.h>
#include <wlr/render/renderer.h>

struct wlr_gles {
	struct wlr_renderer_2 base;
	struct wl_display *display;
	struct wlr_backend *backend;

	struct wlr_gbm_allocator *gbm;
	struct wlr_egl_2 *egl;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC egl_image_target_texture_2d;
	PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC egl_image_target_renderbuffer;

	// Optional extensions
	bool has_required_internalformat;
	bool has_texture_type_2_10_10_10_rev;
	bool has_unpack_subimage;

	struct wlr_format_set shm_formats;
};

struct wlr_gles_rgb_format {
	uint32_t fourcc;
	GLenum format;
	GLenum type;
	bool opaque;
	// Requires GL_EXT_texture_type_2_10_10_10_REV
	bool is_10bit;
};

struct wlr_gles_image {
	EGLImageKHR egl;
	GLuint framebuffer;
	GLuint renderbuffer;
};

enum wlr_gles_texture_type {
	// No client buffer attached
	WLR_GLES_TEXTURE_NULL,
	// Created locally from raw pixels, and not any client buffers
	WLR_GLES_TEXTURE_PIXELS,
	// zwp_linux_dmabuf_v1
	WLR_GLES_TEXTURE_DMABUF,
	// EGL_WL_bind_wayland_display
	WLR_GLES_TEXTURE_EGL,
	// wl_shm
	WLR_GLES_TEXTURE_SHM,
};

struct wlr_gles_texture {
	struct wlr_texture_2 base;
	struct wlr_gles *gles;

	/*
	 * We perform all allocations using GBM, so that we have the possibility
	 * of directy scanning-out as many clients as we possibly can.
	 *
	 * We reuse the wlr_gbm_image type so that we can pass it to
	 * wlr_backend_attach_gbm. The renderer_private field will be NULL,
	 * so it can be distinguished from GBM images created by a
	 * wlr_allocator.
	 */
	struct wlr_gbm_image img;

	enum wlr_gles_texture_type type;
	struct wl_resource *resource;
	const struct wlr_gles_rgb_format *fmt;
	/*
	 * GL_TEXTURE_2D for PIXELS and SHM
	 * GL_TEXTURE_EXTERNAL_OES for DMABUF and EGL
	 *
	 * As such, you cannot call gl*Tex*Image*() on DMABUF/EGL, and they
	 * should be considered immutable. It also affects what shader you need
	 * to render them.
	 *
	 * See the specifications of OES_EGL_image and OES_EGL_image_external
	 * for more information.
	 */
	EGLImageKHR egl;
	GLuint texture;
};

struct wlr_gles_texture *gles_texture_create(struct wlr_gles *gles);

const struct wlr_gles_rgb_format *gles_rgb_format_from_fourcc(
	struct wlr_gles *gles, uint32_t fourcc);
const struct wlr_gles_rgb_format *gles_rgb_format_from_gl(struct wlr_gles *gles,
	GLenum format, GLenum type);
bool gles_populate_shm_formats(struct wlr_gles *gles);

#endif
