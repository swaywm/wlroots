#ifndef _WLR_RENDER_GLES2_INTERNAL_H
#define _WLR_RENDER_GLES2_INTERNAL_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <GLES2/gl2.h>
#include <wlr/render.h>
#include <wlr/util/log.h>

struct pixel_format {
	uint32_t wl_format;
	GLint gl_format, gl_type;
	int depth, bpp;
	GLuint *shader;
};

struct wlr_texture_state {
	struct wlr_texture *wlr_texture;
	GLuint tex_id;
	const struct pixel_format *pixel_format;
};

struct shaders {
	bool initialized;
	GLuint rgba, rgbx;
	GLuint quad;
	GLuint ellipse;
};

extern struct shaders shaders;

const struct pixel_format *gl_format_for_wl_format(enum wl_shm_format fmt);

struct wlr_texture *gles2_texture_init();

extern const GLchar quad_vertex_src[];
extern const GLchar quad_fragment_src[];
extern const GLchar ellipse_fragment_src[];
extern const GLchar vertex_src[];
extern const GLchar fragment_src_rgba[];
extern const GLchar fragment_src_rgbx[];

bool _gles2_flush_errors(const char *file, int line);
#define gles2_flush_errors(...) \
	_gles2_flush_errors(_strip_path(__FILE__), __LINE__)

#define GL_CALL(func) func; gles2_flush_errors()

#endif
