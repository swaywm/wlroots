#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <GLES3/gl3.h>
#include <wlr/render.h>
#include <wlr/render/matrix.h>
#include <wayland-util.h>
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
	if (!_shader->valid) {
		_shader->valid = true;
	} else {
		wl_list_insert(&_shader->link, &shader->link);
	}
	return true;
error:
	if (_shader->valid) {
		wlr_shader_destroy(shader);
	}
	return false;
}

bool wlr_shader_use(struct wlr_shader *shader, uint32_t format) {
	struct wlr_shader *s = shader;
	if (s->format == format) {
		glUseProgram(s->program);
		return true;
	}
	if (shader->link.next) {
		wl_list_for_each(s, &shader->link, link) {
			if (s->format == format) {
				glUseProgram(s->program);
				return true;
			}
		}
	}
	return false;
}

void wlr_shader_destroy(struct wlr_shader *shader) {
	if (!shader) {
		return;
	}
	glDeleteProgram(shader->vert);
	glDeleteProgram(shader->program);
	if (shader->link.next) {
		struct wlr_shader *next = wl_container_of(shader->link.next,
				next, link);
		wlr_shader_destroy(next);
	}
	free(shader);
}
