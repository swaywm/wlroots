#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <GLES3/gl3.h>
#include <wlr/render.h>
#include <wlr/render/matrix.h>
#include "render.h"

static bool create_shader(GLenum type, const GLchar *src, GLint len, GLuint *shader) {
	*shader = glCreateShader(type);
	glShaderSource(*shader, 1, &src, &len);
	glCompileShader(*shader);

	GLint success;
	glGetShaderiv(*shader, GL_COMPILE_STATUS, &success);

	if (success == GL_FALSE) {
		GLint loglen;
		glGetShaderiv(*shader, GL_INFO_LOG_LENGTH, &loglen);

		GLchar msg[loglen];
		glGetShaderInfoLog(*shader, loglen, &loglen, msg);
		return false;
	}
	return true;
}

struct wlr_shader *wlr_shader_init(const char *vertex) {
	struct wlr_shader *shader = calloc(sizeof(struct wlr_shader), 1);
	if (!create_shader(GL_VERTEX_SHADER, vertex, strlen(vertex), &shader->vert)) {
		wlr_shader_destroy(shader);
		return NULL;
	}
	return shader;
}

bool wlr_shader_add_format(struct wlr_shader *shader, uint32_t format,
		const char *frag) {
	assert(shader);
	struct wlr_shader *_shader = shader;
	if (_shader->valid) {
		shader = calloc(sizeof(struct wlr_shader), 1);
		shader->vert = _shader->vert;
	} else {
		while (shader->next) {
			_shader = shader;
			shader = shader->next;
		}
	}
	shader->format = format;
	GLuint _frag;
	if (!create_shader(GL_FRAGMENT_SHADER, frag, strlen(frag), &_frag)) {
		goto error;
	}
	shader->program = glCreateProgram();
	glAttachShader(shader->program, shader->vert);
	glAttachShader(shader->program, _frag);
	glLinkProgram(shader->program);
	glDeleteProgram(_frag);
	if (_shader->valid) {
		_shader->next = shader;
	}
	shader->valid = true;
	return true;
error:
	if (_shader->valid) {
		wlr_shader_destroy(shader);
	}
	return false;
}

bool wlr_shader_use(struct wlr_shader *shader, uint32_t format) {
	if (shader->format == format) {
		glUseProgram(shader->program);
		return true;
	}
	while (shader->next) {
		shader = shader->next;
		if (shader->format == format) {
			glUseProgram(shader->program);
			return true;
		}
	}
	return false;
}

void wlr_shader_destroy(struct wlr_shader *shader) {
	while (shader) {
		struct wlr_shader *_shader = shader;
		glDeleteProgram(shader->vert);
		glDeleteProgram(shader->program);
		shader = shader->next;
		free(_shader);
	}
}
