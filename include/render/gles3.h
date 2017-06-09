#ifndef _WLR_RENDER_GLES2_INTERNAL_H
#define _WLR_RENDER_GLES2_INTERNAL_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <GLES3/gl3.h>
#include <wlr/render.h>

struct wlr_surface_state {
	struct wlr_surface *wlr_surface;
	GLuint tex_id;
};

struct wlr_surface *gles3_surface_init();

extern const GLchar vertex_src[];
extern const GLchar fragment_src_RGB[];
extern const GLchar fragment_src_RGBA[];

bool _gles3_flush_errors(const char *file, int line);
#define gles3_flush_errors(...) \
	_gles3_flush_errors(__FILE__ + strlen(WLR_SRC_DIR) + 1, __LINE__)

#define GL_CALL(func) func; gles3_flush_errors()

#endif
