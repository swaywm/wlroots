#ifndef _WLR_RENDER_INTERNAL_H
#define _WLR_RENDER_INTERNAL_H
#include <stdint.h>
#include <wlr/render.h>
#include <wayland-util.h>
#include <GLES3/gl3.h>
#include <stdbool.h>

struct wlr_surface {
	bool valid;
	GLuint tex_id;
	uint32_t format;
	int width, height;
};

struct wlr_shader {
	bool valid;
	uint32_t format;
	GLuint vert;
	GLuint program;
	struct wlr_shader *next;
};

struct wlr_renderer {
	struct wlr_shader *shader;
	// TODO: EGL stuff
};

#endif
