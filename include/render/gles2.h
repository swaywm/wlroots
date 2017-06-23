#ifndef _WLR_RENDER_GLES2_INTERNAL_H
#define _WLR_RENDER_GLES2_INTERNAL_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <GLES2/gl2.h>
#include <wlr/render.h>

struct wlr_surface_state {
	struct wlr_surface *wlr_surface;
	GLuint tex_id;
};

struct wlr_surface *gles2_surface_init();

extern const GLchar quad_vertex_src[];
extern const GLchar quad_fragment_src[];
extern const GLchar ellipse_fragment_src[];
extern const GLchar vertex_src[];
extern const GLchar fragment_src_RGB[];
extern const GLchar fragment_src_RGBA[];

bool _gles2_flush_errors(const char *file, int line);
#define gles2_flush_errors(...) \
	_gles2_flush_errors(__FILE__ + strlen(WLR_SRC_DIR) + 1, __LINE__)

#define GL_CALL(func) func; gles2_flush_errors()

#endif
