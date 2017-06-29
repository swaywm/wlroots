#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES2/gl2.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

struct shaders shaders;

static bool compile_shader(GLuint type, const GLchar *src, GLuint *shader) {
	*shader = GL_CALL(glCreateShader(type));
	int len = strlen(src);
	GL_CALL(glShaderSource(*shader, 1, &src, &len));
	GL_CALL(glCompileShader(*shader));
	GLint success;
	GL_CALL(glGetShaderiv(*shader, GL_COMPILE_STATUS, &success));
	if (success == GL_FALSE) {
		GLint loglen;
		GL_CALL(glGetShaderiv(*shader, GL_INFO_LOG_LENGTH, &loglen));
		GLchar msg[loglen];
		GL_CALL(glGetShaderInfoLog(*shader, loglen, &loglen, msg));
		wlr_log(L_ERROR, "Shader compilation failed");
		wlr_log(L_ERROR, "%s", msg);
		exit(1);
		return false;
	}
	return true;
}

static bool compile_program(const GLchar *vert_src,
		const GLchar *frag_src, GLuint *program) {
	GLuint vertex, fragment;
	if (!compile_shader(GL_VERTEX_SHADER, vert_src, &vertex)) {
		return false;
	}
	if (!compile_shader(GL_FRAGMENT_SHADER, frag_src, &fragment)) {
		glDeleteProgram(vertex);
		return false;
	}
	*program = GL_CALL(glCreateProgram());
	GL_CALL(glAttachShader(*program, vertex));
	GL_CALL(glAttachShader(*program, fragment));
	GL_CALL(glLinkProgram(*program));
	return true;
}

static void init_default_shaders() {
	if (shaders.initialized) {
		return;
	}
	if (!compile_program(vertex_src, fragment_src_rgba, &shaders.rgba)) {
		goto error;
	}
	if (!compile_program(vertex_src, fragment_src_rgbx, &shaders.rgbx)) {
		goto error;
	}
	if (!compile_program(quad_vertex_src, quad_fragment_src, &shaders.quad)) {
		goto error;
	}
	if (!compile_program(quad_vertex_src, ellipse_fragment_src, &shaders.ellipse)) {
		goto error;
	}
	wlr_log(L_DEBUG, "Compiled default shaders");
	return;
error:
	wlr_log(L_ERROR, "Failed to set up default shaders!");
}

static void init_globals() {
	init_default_shaders();
}

static void wlr_gles2_begin(struct wlr_renderer_state *state,
		struct wlr_output *output) {
	// TODO: let users customize the clear color?
	GL_CALL(glClearColor(0.25f, 0.25f, 0.25f, 1));
	GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
	int32_t width = output->width;
	int32_t height = output->height;
	GL_CALL(glViewport(0, 0, width, height));
	// Note: maybe we should save output projection and remove some of the need
	// for users to sling matricies themselves
}

static void wlr_gles2_end(struct wlr_renderer_state *state) {
	// no-op
}

static struct wlr_surface *wlr_gles2_surface_init(struct wlr_renderer_state *state) {
	return gles2_surface_init();
}

static void draw_quad() {
	GLfloat verts[] = {
		1, 0, // top right
		0, 0, // top left
		1, 1, // bottom right
		0, 1, // bottom left
	};
	GLfloat texcoord[] = {
		1, 0, // top right
		0, 0, // top left
		1, 1, // bottom right
		0, 1, // bottom left
	};

	GL_CALL(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts));
	GL_CALL(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoord));

	GL_CALL(glEnableVertexAttribArray(0));
	GL_CALL(glEnableVertexAttribArray(1));

	GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

	GL_CALL(glDisableVertexAttribArray(0));
	GL_CALL(glDisableVertexAttribArray(1));
}

static bool wlr_gles2_render_surface(struct wlr_renderer_state *state,
		struct wlr_surface *surface, const float (*matrix)[16]) {
	assert(surface && surface->valid);
	wlr_surface_bind(surface);
	GL_CALL(glUniformMatrix4fv(0, 1, GL_FALSE, *matrix));
	// TODO: source alpha from somewhere else I guess
	GL_CALL(glUniform1f(2, 1.0f));
	draw_quad();
	return true;
}

static void wlr_gles2_render_quad(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
	GL_CALL(glUseProgram(shaders.quad));
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glUniform4f(1, (*color)[0], (*color)[1], (*color)[2], (*color)[3]));
	draw_quad();
}

static void wlr_gles2_render_ellipse(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
	GL_CALL(glUseProgram(shaders.ellipse));
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glUniform4f(1, (*color)[0], (*color)[1], (*color)[2], (*color)[3]));
	draw_quad();
}

static const enum wl_shm_format *wlr_gles2_formats(
		struct wlr_renderer_state *state, size_t *len) {
	static enum wl_shm_format formats[] = {
		WL_SHM_FORMAT_ARGB8888,
		WL_SHM_FORMAT_XRGB8888,
		WL_SHM_FORMAT_ABGR8888,
		WL_SHM_FORMAT_XBGR8888,
	};
	*len = sizeof(formats) / sizeof(formats[0]);
	return formats;
}

static void wlr_gles2_destroy(struct wlr_renderer_state *state) {
	// no-op
}

static struct wlr_renderer_impl wlr_renderer_impl = {
	.begin = wlr_gles2_begin,
	.end = wlr_gles2_end,
	.surface_init = wlr_gles2_surface_init,
	.render_with_matrix = wlr_gles2_render_surface,
	.render_quad = wlr_gles2_render_quad,
	.render_ellipse = wlr_gles2_render_ellipse,
	.formats = wlr_gles2_formats,
	.destroy = wlr_gles2_destroy
};

struct wlr_renderer *wlr_gles2_renderer_init() {
	init_globals();
	return wlr_renderer_init(NULL, &wlr_renderer_impl);
}
