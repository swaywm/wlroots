#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H

#include <stdbool.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>

#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/render.h>
#include <wlr/types/wlr_output.h>

struct wlr_render {
	struct wlr_egl *egl;

	float proj[9];

	struct {
		GLuint poly;
		GLuint tex;
		GLuint ext;
	} shaders;
};

struct wlr_tex {
	struct wlr_render *rend;

	EGLImageKHR image;
	GLuint image_tex;
	uint32_t width;
	uint32_t height;

	enum {
		WLR_TEX_GLTEX,
		WLR_TEX_WLDRM_GL,
		WLR_TEX_WLDRM_EXT,
		WLR_TEX_DMABUF,
	} type;
	union {
		GLuint gl_tex;
		struct wl_resource *wl_drm;
		int dmabuf;
	};
};

struct format {
	enum wl_shm_format wl_fmt;
	GLuint gl_fmt;
	GLuint gl_type;
	uint32_t bpp;
};

const struct format *wl_to_gl(enum wl_shm_format fmt);

struct wlr_render *wlr_render_create(struct wlr_backend *backend);
void wlr_render_destroy(struct wlr_render *rend);

void wlr_render_bind_raw(struct wlr_render *rend, uint32_t width, uint32_t height,
		enum wl_output_transform transform);

void push_marker(const char *file, const char *func);
void pop_marker(void);
#define DEBUG_PUSH push_marker(_strip_path(__FILE__), __func__)
#define DEBUG_POP pop_marker()

#endif
