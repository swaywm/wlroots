#ifndef RENDER_GLES2_H
#define RENDER_GLES2_H

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
#include <wlr/util/log.h>

struct wlr_gles2_procs {
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
	PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
	PFNGLDEBUGMESSAGECONTROLKHRPROC glDebugMessageControlKHR;
	PFNGLPOPDEBUGGROUPKHRPROC glPopDebugGroupKHR;
	PFNGLPUSHDEBUGGROUPKHRPROC glPushDebugGroupKHR;
};

extern struct wlr_gles2_procs gles2_procs;

struct wlr_gles2_pixel_format {
	enum wl_shm_format wl_format;
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

	struct wlr_egl *egl;

	const char *exts_str;
	struct {
		bool read_format_bgra_ext;
		bool debug_khr;
		bool egl_image_external_oes;
	} exts;

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

struct wlr_gles2_texture {
	struct wlr_texture wlr_texture;
	struct wlr_egl *egl;

	// Basically:
	//   GL_TEXTURE_2D == mutable
	//   GL_TEXTURE_EXTERNAL_OES == immutable
	GLenum target;
	GLuint tex;

	EGLImageKHR image;

	bool inverted_y;
	bool has_alpha;

	// Only affects target == GL_TEXTURE_2D
	enum wl_shm_format wl_format; // used to interpret upload data
};

const struct wlr_gles2_pixel_format *get_gles2_format_from_wl(
	enum wl_shm_format fmt);
const struct wlr_gles2_pixel_format *get_gles2_format_from_gl(
	GLint gl_format, GLint gl_type, bool alpha);
const enum wl_shm_format *get_gles2_wl_formats(size_t *len);

struct wlr_gles2_texture *gles2_get_texture(
	struct wlr_texture *wlr_texture);

void push_gles2_marker(const char *file, const char *func);
void pop_gles2_marker(void);
#define PUSH_GLES2_DEBUG push_gles2_marker(_WLR_FILENAME, __func__)
#define POP_GLES2_DEBUG pop_gles2_marker()

#endif
