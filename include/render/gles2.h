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
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>

extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

struct gles2_pixel_format {
	uint32_t wl_format;
	GLint gl_format, gl_type;
	int depth, bpp;
};

struct wlr_gles2_renderer {
	struct wlr_renderer wlr_renderer;

	struct wlr_egl *egl;

	struct {
		GLuint quad;
		GLuint ellipse;
		GLuint tex_rgba;
		GLuint tex_rgbx;
		GLuint tex_ext;
	} shaders;
};

struct wlr_gles2_texture {
	struct wlr_texture wlr_texture;

	struct wlr_egl *egl;
	GLuint tex_id;
	const struct gles2_pixel_format *pixel_format;
	EGLImageKHR image;
	GLenum target;
};

const struct gles2_pixel_format *gles2_format_from_wl(enum wl_shm_format fmt);

struct wlr_texture *gles2_texture_create();
struct wlr_gles2_texture *gles2_get_texture(struct wlr_texture *wlr_texture);

void gles2_push_marker(const char *file, const char *func);
void gles2_pop_marker(void);
#define GLES2_DEBUG_PUSH gles2_push_marker(wlr_strip_path(__FILE__), __func__)
#define GLES2_DEBUG_POP gles2_pop_marker()

#endif
