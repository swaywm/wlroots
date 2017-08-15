#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/egl.h>
#include <wlr/backend.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = NULL;
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
		glDeleteShader(*shader);
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
		glDeleteShader(vertex);
		return false;
	}
	*program = GL_CALL(glCreateProgram());
	GL_CALL(glAttachShader(*program, vertex));
	GL_CALL(glAttachShader(*program, fragment));
	GL_CALL(glLinkProgram(*program));
	GLint success;
	GL_CALL(glGetProgramiv(*program, GL_LINK_STATUS, &success));
	if (success == GL_FALSE) {
		GLint loglen;
		GL_CALL(glGetProgramiv(*program, GL_INFO_LOG_LENGTH, &loglen));
		GLchar msg[loglen];
		GL_CALL(glGetProgramInfoLog(*program, loglen, &loglen, msg));
		wlr_log(L_ERROR, "Program link failed");
		wlr_log(L_ERROR, "%s", msg);
		glDeleteProgram(*program);
		glDeleteShader(vertex);
		glDeleteShader(fragment);
		return false;
	}
	glDetachShader(*program, vertex);
	glDetachShader(*program, fragment);
	glDeleteShader(vertex);
	glDeleteShader(fragment);

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
	if (glEGLImageTargetTexture2DOES) {
		if (!compile_program(quad_vertex_src, fragment_src_external, &shaders.external)) {
			goto error;
		}
	}

	wlr_log(L_DEBUG, "Compiled default shaders");
	return;
error:
	wlr_log(L_ERROR, "Failed to set up default shaders!");
}

static void init_image_ext() {
	if (glEGLImageTargetTexture2DOES)
		return;

	const char *exts = (const char*) glGetString(GL_EXTENSIONS);
	if (strstr(exts, "GL_OES_EGL_image_external")) {
 		glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
			eglGetProcAddress("glEGLImageTargetTexture2DOES");
	}

	if (!glEGLImageTargetTexture2DOES) {
		wlr_log(L_INFO, "Failed to load glEGLImageTargetTexture2DOES "
			"Will not be able to attach drm buffers");
	}
}

static void init_globals() {
	init_image_ext();
	init_default_shaders();
}

static void wlr_gles2_begin(struct wlr_renderer *_renderer,
		struct wlr_output *output) {
	// TODO: let users customize the clear color?
	GL_CALL(glClearColor(0.25f, 0.25f, 0.25f, 1));
	GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
	int32_t width = output->width;
	int32_t height = output->height;
	GL_CALL(glViewport(0, 0, width, height));

	// enable transparency
	GL_CALL(glEnable(GL_BLEND));
	GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

	// Note: maybe we should save output projection and remove some of the need
	// for users to sling matricies themselves
}

static void wlr_gles2_end(struct wlr_renderer *renderer) {
	// no-op
}

static struct wlr_texture *wlr_gles2_texture_init(
		struct wlr_renderer *_renderer) {
	struct wlr_gles2_renderer *renderer =
		(struct wlr_gles2_renderer *)_renderer;
	return gles2_texture_init(renderer->egl);
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

static bool wlr_gles2_render_texture(struct wlr_renderer *_renderer,
		struct wlr_texture *texture, const float (*matrix)[16]) {
	if(!texture || !texture->valid) {
		wlr_log(L_ERROR, "attempt to render invalid texture");
		return false;
	}

	wlr_texture_bind(texture);
	GL_CALL(glUniformMatrix4fv(0, 1, GL_FALSE, *matrix));
	// TODO: source alpha from somewhere else I guess
	GL_CALL(glUniform1f(2, 1.0f));
	draw_quad();
	return true;
}

static void wlr_gles2_render_quad(struct wlr_renderer *renderer,
		const float (*color)[4], const float (*matrix)[16]) {
	GL_CALL(glUseProgram(shaders.quad));
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glUniform4f(1, (*color)[0], (*color)[1], (*color)[2], (*color)[3]));
	draw_quad();
}

static void wlr_gles2_render_ellipse(struct wlr_renderer *renderer,
		const float (*color)[4], const float (*matrix)[16]) {
	GL_CALL(glUseProgram(shaders.ellipse));
	GL_CALL(glUniformMatrix4fv(0, 1, GL_TRUE, *matrix));
	GL_CALL(glUniform4f(1, (*color)[0], (*color)[1], (*color)[2], (*color)[3]));
	draw_quad();
}

static const enum wl_shm_format *wlr_gles2_formats(
		struct wlr_renderer *renderer, size_t *len) {
	static enum wl_shm_format formats[] = {
		WL_SHM_FORMAT_ARGB8888,
		WL_SHM_FORMAT_XRGB8888,
		WL_SHM_FORMAT_ABGR8888,
		WL_SHM_FORMAT_XBGR8888,
	};
	*len = sizeof(formats) / sizeof(formats[0]);
	return formats;
}

static bool wlr_gles2_buffer_is_drm(struct wlr_renderer *_renderer,
		struct wl_resource *buffer) {
	struct wlr_gles2_renderer *renderer =
		(struct wlr_gles2_renderer *)_renderer;
	EGLint format;
	return wlr_egl_query_buffer(renderer->egl, buffer,
			EGL_TEXTURE_FORMAT, &format);
}

static struct wlr_renderer_impl wlr_renderer_impl = {
	.begin = wlr_gles2_begin,
	.end = wlr_gles2_end,
	.texture_init = wlr_gles2_texture_init,
	.render_with_matrix = wlr_gles2_render_texture,
	.render_quad = wlr_gles2_render_quad,
	.render_ellipse = wlr_gles2_render_ellipse,
	.formats = wlr_gles2_formats,
	.buffer_is_drm = wlr_gles2_buffer_is_drm,
};

struct wlr_renderer *wlr_gles2_renderer_init(struct wlr_backend *backend) {
	init_globals();
	struct wlr_gles2_renderer *renderer;
	if (!(renderer = calloc(1, sizeof(struct wlr_gles2_renderer)))) {
		return NULL;
	}
	wlr_renderer_init(&renderer->wlr_renderer, &wlr_renderer_impl);
	if (backend) {
		struct wlr_egl *egl = wlr_backend_get_egl(backend);
		renderer->egl = egl;
	}
	return &renderer->wlr_renderer;
}
