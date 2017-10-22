#ifndef RENDER_RENDER_H
#define RENDER_RENDER_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>

#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_output.h>

struct wlr_render {
	struct wlr_egl *egl;

	float proj[9];

	struct {
		GLuint quad;
		GLuint ellipse;
		GLuint rgba;
		GLuint rgbx;
		GLuint extn;
	} shaders;
};

struct wlr_tex {
	struct wlr_render *rend;

	EGLImageKHR image;
	GLuint image_tex;
	enum wl_shm_format fmt;
	uint32_t width;
	uint32_t height;

	enum {
		WLR_TEX_GLTEX,
	} type;
	union {
		GLuint gl_tex;
	};
};

struct wlr_render *wlr_render_create(struct wlr_backend *backend);
void wlr_render_destroy(struct wlr_render *rend);

void wlr_render_bind(struct wlr_render *rend, struct wlr_output *output);
void wlr_render_clear(struct wlr_render *rend, float r, float g, float b, float a);

void wlr_render_subtexture(struct wlr_render *rend, struct wlr_tex *tex,
	int32_t tex_x1, int32_t tex_y1, int32_t tex_x2, int32_t tex_y2,
	int32_t pos_x1, int32_t pos_y1, int32_t pos_x2, int32_t pos_y2, int32_t pos_z);

void wlr_render_texture(struct wlr_render *rend, struct wlr_tex *tex,
	int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t z);

struct wlr_tex *wlr_tex_from_pixels(struct wlr_render *rend, enum wl_shm_format fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data);

void push_marker(const char *file, const char *func);
void pop_marker(void);
#define DEBUG_PUSH push_marker(_strip_path(__FILE__), __func__)
#define DEBUG_POP pop_marker()

#endif
