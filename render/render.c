#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "render/render.h"
#include "render/glapi.h"

void wlr_render_bind(struct wlr_render *rend, struct wlr_output *output) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	glViewport(0, 0, output->width, output->height);

	DEBUG_POP;
}

void wlr_render_clear(struct wlr_render *rend, float r, float g, float b, float a) {
	assert(eglGetCurrentContext() == rend->egl->context);
	DEBUG_PUSH;

	glClearColor(r, g, b, a);
	glClear(GL_COLOR_BUFFER_BIT);

	DEBUG_POP;
}

void push_marker(const char *file, const char *func) {
	if (!glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_marker(void) {
	if (glPopDebugGroupKHR) {
		glPopDebugGroupKHR();
	}
}

static log_importance_t gl_to_wlr(GLenum type) {
	switch (type) {
	case GL_DEBUG_TYPE_ERROR_KHR:               return L_ERROR;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR: return L_DEBUG;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:  return L_ERROR;
	case GL_DEBUG_TYPE_PORTABILITY_KHR:         return L_DEBUG;
	case GL_DEBUG_TYPE_PERFORMANCE_KHR:         return L_DEBUG;
	case GL_DEBUG_TYPE_OTHER_KHR:               return L_INFO;
	case GL_DEBUG_TYPE_MARKER_KHR:              return L_DEBUG;
	case GL_DEBUG_TYPE_PUSH_GROUP_KHR:          return L_DEBUG;
	case GL_DEBUG_TYPE_POP_GROUP_KHR:           return L_DEBUG;
	default:                                    return L_INFO;
	}
}

static void gl_log(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const GLchar *msg, const void *user) {
	_wlr_log(gl_to_wlr(type), "[GLES] %s", msg);
}

static GLuint compile_shader(GLuint type, const GLchar *src) {
	DEBUG_PUSH;
	GLuint shader = glCreateShader(type);

	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success == GL_FALSE) {
		glDeleteShader(shader);
		shader = 0;
	}

	DEBUG_POP;
	return shader;
}

static GLuint link_program(const GLchar *vert_src, const GLchar *frag_src) {
	DEBUG_PUSH;
	GLuint prog = 0;

	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vert) {
		goto error;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!frag) {
		glDeleteShader(vert);
		goto error;
	}

	prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint success;
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if (success == GL_FALSE) {
		glDeleteProgram(prog);
		prog = 0;
	}

error:
	DEBUG_POP;
	return prog;
}

extern const GLchar quad_vert_src[];
extern const GLchar quad_frag_src[];
extern const GLchar ellipse_frag_src[];
extern const GLchar tex_vert_src[];
extern const GLchar rgba_frag_src[];
extern const GLchar rgbx_frag_src[];
extern const GLchar extn_frag_src[];

struct wlr_render *wlr_render_create(struct wlr_backend *backend) {
	struct wlr_render *rend = calloc(1, sizeof(*rend));
	if (!rend) {
		return NULL;
	}

	rend->egl = wlr_backend_get_egl(backend);
	eglMakeCurrent(rend->egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		rend->egl->context);

	if (glDebugMessageCallbackKHR && glDebugMessageControlKHR) {
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		glDebugMessageCallbackKHR(gl_log, NULL);

		// Silence unwanted message types
		glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP_KHR,
			GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP_KHR,
			GL_DONT_CARE, 0, NULL, GL_FALSE);
	}

	DEBUG_PUSH;

	rend->shaders.quad = link_program(quad_vert_src, quad_frag_src);
	if (!rend->shaders.quad) {
		goto error;
	}
	rend->shaders.ellipse = link_program(quad_vert_src, ellipse_frag_src);
	if (!rend->shaders.ellipse) {
		goto error;
	}
	rend->shaders.rgba = link_program(tex_vert_src, rgba_frag_src);
	if (!rend->shaders.rgba) {
		goto error;
	}
	rend->shaders.rgbx = link_program(tex_vert_src, rgbx_frag_src);
	if (!rend->shaders.rgbx) {
		goto error;
	}
	rend->shaders.extn = link_program(tex_vert_src, extn_frag_src);
	if (!rend->shaders.extn) {
		goto error;
	}

	DEBUG_POP;
	return rend;

error:
	glDeleteProgram(rend->shaders.quad);
	glDeleteProgram(rend->shaders.ellipse);
	glDeleteProgram(rend->shaders.rgba);
	glDeleteProgram(rend->shaders.rgbx);
	glDeleteProgram(rend->shaders.extn);

	DEBUG_POP;
	if (glDebugMessageCallbackKHR) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		glDebugMessageCallbackKHR(NULL, NULL);
	}

	free(rend);
	return NULL;
}

void wlr_render_destroy(struct wlr_render *rend) {
	free(rend);
}
