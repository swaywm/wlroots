#ifndef RENDER_GLES2_H
#define RENDER_GLES2_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/wlr_render_surface.h>
#include <wlr/util/log.h>

extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

struct wlr_gles2_pixel_format {
	uint32_t wl_format;
	GLint gl_format, gl_type;
	int depth, bpp;
	bool has_alpha;
};

struct wlr_gles2_tex_shader {
	GLuint program;
	GLint proj;
	GLint invert_y;
	GLint tex;
	GLint alpha;
};

struct wlr_gles2_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;

	struct wlr_egl egl;
	const char *exts_str;

	struct {
		struct {
			GLuint program;
			GLint proj;
			GLint color;
		} quad;
		struct {
			GLuint program;
			GLint proj;
			GLint color;
		} ellipse;
		struct wlr_gles2_tex_shader tex_rgba;
		struct wlr_gles2_tex_shader tex_rgbx;
		struct wlr_gles2_tex_shader tex_ext;
	} shaders;

	uint32_t viewport_width, viewport_height;
};

enum wlr_gles2_texture_type {
	WLR_GLES2_TEXTURE_GLTEX,
	WLR_GLES2_TEXTURE_WL_DRM_GL,
	WLR_GLES2_TEXTURE_WL_DRM_EXT,
	WLR_GLES2_TEXTURE_DMABUF,
};

struct wlr_gles2_texture {
	struct wlr_texture wlr_texture;

	struct wlr_egl *egl;
	enum wlr_gles2_texture_type type;
	int width, height;
	bool has_alpha;
	bool inverted_y;

	// Not set if WLR_GLES2_TEXTURE_GLTEX
	EGLImageKHR image;
	GLuint image_tex;

	union {
		GLuint gl_tex;
		struct wl_resource *wl_drm;
	};
};

struct wlr_gles2_render_surface {
	struct wlr_render_surface render_surface;
	struct wlr_gles2_renderer *renderer;

	// needed e.g. for resizing the surface
	// x11: xcb_window_t
	// wayland: wl_surface*
	// dummy: NULL
	// drm: wlr_drm_surface*
	void* handle;
	struct gbm_surface *gbm_surface; // only for drm
	struct wl_egl_window *egl_window; // only for wayland

	EGLSurface surface;
	uint32_t width;
	uint32_t height;
};

const struct wlr_gles2_pixel_format *get_gles2_format_from_wl(
	enum wl_shm_format fmt);
const enum wl_shm_format *get_gles2_formats(size_t *len);

struct wlr_gles2_texture *gles2_get_texture(
	struct wlr_texture *wlr_texture);

void push_gles2_marker(const char *file, const char *func);
void pop_gles2_marker(void);
#define PUSH_GLES2_DEBUG push_gles2_marker(_wlr_strip_path(__FILE__), __func__)
#define POP_GLES2_DEBUG pop_gles2_marker()

#endif
