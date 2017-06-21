#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES3/gl3.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "render/gles3.h"

static struct {
	bool initialized;
	GLuint rgb, rgba;
	GLuint quad;
	GLuint ellipse;
} shaders;
static GLuint vao, vbo, ebo;

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
	if (!compile_program(vertex_src, fragment_src_RGB, &shaders.rgb)) {
		goto error;
	}
	if (!compile_program(vertex_src, fragment_src_RGBA, &shaders.rgba)) {
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

static void init_default_quad() {
	GLfloat verticies[] = {
		1, 1, 1, 1, // bottom right
		1, 0, 1, 0, // top right
		0, 0, 0, 0, // top left
		0, 1, 0, 1, // bottom left
	};
	GLuint indicies[] = {
		0, 1, 3,
		1, 2, 3,
	};

	GL_CALL(glGenVertexArrays(1, &vao));
	GL_CALL(glGenBuffers(1, &vbo));

	GL_CALL(glBindVertexArray(vao));
	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo));

	GL_CALL(glEnableVertexAttribArray(0));
	GL_CALL(glEnableVertexAttribArray(1));
	GL_CALL(glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0));
	GL_CALL(glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat))));
	GL_CALL(glBufferData(GL_ARRAY_BUFFER, sizeof(verticies), verticies, GL_STATIC_DRAW));

	GL_CALL(glGenBuffers(1, &ebo));
	GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
	GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indicies), indicies, GL_STATIC_DRAW));
}

static void init_globals() {
	init_default_shaders();
	init_default_quad();
}

static void wlr_gles3_begin(struct wlr_renderer_state *state,
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

static void wlr_gles3_end(struct wlr_renderer_state *state) {
	// no-op
}

static struct wlr_surface *wlr_gles3_surface_init(struct wlr_renderer_state *state) {
	return gles3_surface_init();
}

static bool wlr_gles3_render_surface(struct wlr_renderer_state *state,
		struct wlr_surface *surface, const float (*matrix)[16]) {
	assert(surface && surface->valid);
	switch (surface->format) {
	case GL_RGB:
		GL_CALL(glUseProgram(shaders.rgb));
		break;
	case GL_RGBA:
		GL_CALL(glUseProgram(shaders.rgba));
		break;
	default:
		wlr_log(L_ERROR, "No shader for this surface format");
		return false;
	}
	gles3_flush_errors();
	GL_CALL(glBindVertexArray(vao));
	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo));
	GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
	wlr_surface_bind(surface);
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0));
	return true;
}

static void wlr_gles3_render_quad(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
	GL_CALL(glUseProgram(shaders.quad));
	GL_CALL(glBindVertexArray(vao));
	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo));
	GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glUniform4f(1, (*color)[0], (*color)[1], (*color)[2], (*color)[3]));
	GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0));
}

static void wlr_gles3_render_ellipse(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
	GL_CALL(glUseProgram(shaders.ellipse));
	GL_CALL(glBindVertexArray(vao));
	GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, vbo));
	GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo));
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glUniform4f(1, (*color)[0], (*color)[1], (*color)[2], (*color)[3]));
	GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0));
}

static void wlr_gles3_destroy(struct wlr_renderer_state *state) {
	// no-op
}

static struct wlr_renderer_impl wlr_renderer_impl = {
	.begin = wlr_gles3_begin,
	.end = wlr_gles3_end,
	.surface_init = wlr_gles3_surface_init,
	.render_with_matrix = wlr_gles3_render_surface,
	.render_quad = wlr_gles3_render_quad,
	.render_ellipse = wlr_gles3_render_ellipse,
	.destroy = wlr_gles3_destroy
};

struct wlr_renderer *wlr_gles3_renderer_init() {
	init_globals();
	return wlr_renderer_init(NULL, &wlr_renderer_impl);
}
