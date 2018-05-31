#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "glapi.h"
#include "render/gles2.h"

static const struct wlr_renderer_impl renderer_impl;

static struct wlr_gles2_renderer *gles2_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_gles2_renderer *)wlr_renderer;
}

static struct wlr_gles2_renderer *gles2_get_renderer_in_context(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	assert(wlr_egl_is_current(renderer->egl));
	return renderer;
}

static void gles2_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	PUSH_GLES2_DEBUG;

	glViewport(0, 0, width, height);
	renderer->viewport_width = width;
	renderer->viewport_height = height;

	// enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// XXX: maybe we should save output projection and remove some of the need
	// for users to sling matricies themselves

	POP_GLES2_DEBUG;
}

static void gles2_end(struct wlr_renderer *wlr_renderer) {
	gles2_get_renderer_in_context(wlr_renderer);
	// no-op
}

static void gles2_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	gles2_get_renderer_in_context(wlr_renderer);

	PUSH_GLES2_DEBUG;
	glClearColor(color[0], color[1], color[2], color[3]);
	glClear(GL_COLOR_BUFFER_BIT);
	POP_GLES2_DEBUG;
}

static void gles2_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	PUSH_GLES2_DEBUG;
	if (box != NULL) {
		struct wlr_box gl_box;
		wlr_box_transform(box, WL_OUTPUT_TRANSFORM_FLIPPED_180,
			renderer->viewport_width, renderer->viewport_height, &gl_box);

		glScissor(gl_box.x, gl_box.y, gl_box.width, gl_box.height);
		glEnable(GL_SCISSOR_TEST);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
	POP_GLES2_DEBUG;
}

static void draw_quad(void) {
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

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
}

static bool gles2_render_texture_with_matrix(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *wlr_texture, const float matrix[static 9],
		float alpha) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);
	struct wlr_gles2_texture *texture =
		get_gles2_texture_in_context(wlr_texture);

	GLuint prog = 0;
	GLenum target = 0;
	switch (texture->type) {
	case WLR_GLES2_TEXTURE_GLTEX:
	case WLR_GLES2_TEXTURE_WL_DRM_GL:
		prog = texture->has_alpha ? renderer->shaders.tex_rgba :
			renderer->shaders.tex_rgbx;
		target = GL_TEXTURE_2D;
		break;
	case WLR_GLES2_TEXTURE_WL_DRM_EXT:
	case WLR_GLES2_TEXTURE_DMABUF:
		prog = renderer->shaders.tex_ext;
		target = GL_TEXTURE_EXTERNAL_OES;
		break;
	}

	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	PUSH_GLES2_DEBUG;

	GLuint tex_id = texture->type == WLR_GLES2_TEXTURE_GLTEX ?
		texture->gl_tex : texture->image_tex;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(target, tex_id);

	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glUseProgram(prog);

	glUniformMatrix3fv(0, 1, GL_FALSE, transposition);
	glUniform1i(1, texture->inverted_y);
	glUniform1f(3, alpha);

	draw_quad();

	POP_GLES2_DEBUG;
	return true;
}


static void gles2_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	PUSH_GLES2_DEBUG;
	glUseProgram(renderer->shaders.quad);
	glUniformMatrix3fv(0, 1, GL_FALSE, transposition);
	glUniform4f(1, color[0], color[1], color[2], color[3]);
	draw_quad();
	POP_GLES2_DEBUG;
}

static void gles2_render_ellipse_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	PUSH_GLES2_DEBUG;
	glUseProgram(renderer->shaders.ellipse);
	glUniformMatrix3fv(0, 1, GL_FALSE, transposition);
	glUniform4f(1, color[0], color[1], color[2], color[3]);
	draw_quad();
	POP_GLES2_DEBUG;
}

static const enum wl_shm_format *gles2_renderer_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	return get_gles2_formats(len);
}

static bool gles2_resource_is_wl_drm_buffer(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	if (!eglQueryWaylandBufferWL) {
		return false;
	}

	EGLint fmt;
	return eglQueryWaylandBufferWL(renderer->egl->display, resource,
		EGL_TEXTURE_FORMAT, &fmt);
}

static void gles2_wl_drm_buffer_get_size(struct wlr_renderer *wlr_renderer,
		struct wl_resource *buffer, int *width, int *height) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	if (!eglQueryWaylandBufferWL) {
		return;
	}

	eglQueryWaylandBufferWL(renderer->egl->display, buffer, EGL_WIDTH, width);
	eglQueryWaylandBufferWL(renderer->egl->display, buffer, EGL_HEIGHT, height);
}

static int gles2_get_dmabuf_formats(struct wlr_renderer *wlr_renderer,
		int **formats) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return wlr_egl_get_dmabuf_formats(renderer->egl, formats);
}

static int gles2_get_dmabuf_modifiers(struct wlr_renderer *wlr_renderer,
		int format, uint64_t **modifiers) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return wlr_egl_get_dmabuf_modifiers(renderer->egl, format, modifiers);
}

static bool gles2_read_pixels(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width,
		uint32_t height, uint32_t src_x, uint32_t src_y, uint32_t dst_x,
		uint32_t dst_y, void *data) {
	gles2_get_renderer_in_context(wlr_renderer);

	const struct wlr_gles2_pixel_format *fmt = get_gles2_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(L_ERROR, "Cannot read pixels: unsupported pixel format");
		return false;
	}

	PUSH_GLES2_DEBUG;

	// Make sure any pending drawing is finished before we try to read it
	glFinish();

	// Unfortunately GLES2 doesn't support GL_PACK_*, so we have to read
	// the lines out row by row
	unsigned char *p = data + dst_y * stride;
	for (size_t i = src_y; i < src_y + height; ++i) {
		glReadPixels(src_x, src_y + height - i - 1, width, 1, fmt->gl_format,
			fmt->gl_type, p + i * stride + dst_x * fmt->bpp / 8);
	}

	POP_GLES2_DEBUG;

	return true;
}

static bool gles2_format_supported(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt) {
	return get_gles2_format_from_wl(wl_fmt) != NULL;
}

static struct wlr_texture *gles2_texture_from_pixels(
		struct wlr_renderer *wlr_renderer, enum wl_shm_format wl_fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return wlr_gles2_texture_from_pixels(renderer->egl, wl_fmt, stride, width,
		height, data);
}

static struct wlr_texture *gles2_texture_from_wl_drm(
		struct wlr_renderer *wlr_renderer, struct wl_resource *data) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return wlr_gles2_texture_from_wl_drm(renderer->egl, data);
}

static struct wlr_texture *gles2_texture_from_dmabuf(
		struct wlr_renderer *wlr_renderer,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return wlr_gles2_texture_from_dmabuf(renderer->egl, attribs);
}

static void gles2_init_wl_display(struct wlr_renderer *wlr_renderer,
		struct wl_display *wl_display) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);
	if (!wlr_egl_bind_display(renderer->egl, wl_display)) {
		wlr_log(L_INFO, "failed to bind wl_display to EGL");
	}
}

static void gles2_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	wlr_egl_make_current(renderer->egl, EGL_NO_SURFACE, NULL);

	PUSH_GLES2_DEBUG;
	glDeleteProgram(renderer->shaders.quad);
	glDeleteProgram(renderer->shaders.ellipse);
	glDeleteProgram(renderer->shaders.tex_rgba);
	glDeleteProgram(renderer->shaders.tex_rgbx);
	glDeleteProgram(renderer->shaders.tex_ext);
	POP_GLES2_DEBUG;

	if (glDebugMessageCallbackKHR) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		glDebugMessageCallbackKHR(NULL, NULL);
	}

	free(renderer);
}

static const struct wlr_renderer_impl renderer_impl = {
	.destroy = gles2_destroy,
	.begin = gles2_begin,
	.end = gles2_end,
	.clear = gles2_clear,
	.scissor = gles2_scissor,
	.render_texture_with_matrix = gles2_render_texture_with_matrix,
	.render_quad_with_matrix = gles2_render_quad_with_matrix,
	.render_ellipse_with_matrix = gles2_render_ellipse_with_matrix,
	.formats = gles2_renderer_formats,
	.resource_is_wl_drm_buffer = gles2_resource_is_wl_drm_buffer,
	.wl_drm_buffer_get_size = gles2_wl_drm_buffer_get_size,
	.get_dmabuf_formats = gles2_get_dmabuf_formats,
	.get_dmabuf_modifiers = gles2_get_dmabuf_modifiers,
	.read_pixels = gles2_read_pixels,
	.format_supported = gles2_format_supported,
	.texture_from_pixels = gles2_texture_from_pixels,
	.texture_from_wl_drm = gles2_texture_from_wl_drm,
	.texture_from_dmabuf = gles2_texture_from_dmabuf,
	.init_wl_display = gles2_init_wl_display,
};

void push_gles2_marker(const char *file, const char *func) {
	if (!glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_gles2_marker(void) {
	if (glPopDebugGroupKHR) {
		glPopDebugGroupKHR();
	}
}

static log_importance_t gles2_log_importance_to_wlr(GLenum type) {
	switch (type) {
	case GL_DEBUG_TYPE_ERROR_KHR:               return L_ERROR;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR: return L_DEBUG;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:  return L_ERROR;
	case GL_DEBUG_TYPE_PORTABILITY_KHR:         return L_DEBUG;
	case GL_DEBUG_TYPE_PERFORMANCE_KHR:         return L_DEBUG;
	case GL_DEBUG_TYPE_OTHER_KHR:               return L_DEBUG;
	case GL_DEBUG_TYPE_MARKER_KHR:              return L_DEBUG;
	case GL_DEBUG_TYPE_PUSH_GROUP_KHR:          return L_DEBUG;
	case GL_DEBUG_TYPE_POP_GROUP_KHR:           return L_DEBUG;
	default:                                    return L_DEBUG;
	}
}

static void gles2_log(GLenum src, GLenum type, GLuint id, GLenum severity,
		GLsizei len, const GLchar *msg, const void *user) {
	_wlr_log(gles2_log_importance_to_wlr(type), "[GLES2] %s", msg);
}

static GLuint compile_shader(GLuint type, const GLchar *src) {
	PUSH_GLES2_DEBUG;

	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteShader(shader);
		shader = 0;
	}

	POP_GLES2_DEBUG;
	return shader;
}

static GLuint link_program(const GLchar *vert_src, const GLchar *frag_src) {
	PUSH_GLES2_DEBUG;

	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	if (!vert) {
		goto error;
	}

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	if (!frag) {
		glDeleteShader(vert);
		goto error;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vert);
	glAttachShader(prog, frag);
	glLinkProgram(prog);

	glDetachShader(prog, vert);
	glDetachShader(prog, frag);
	glDeleteShader(vert);
	glDeleteShader(frag);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (ok == GL_FALSE) {
		glDeleteProgram(prog);
		goto error;
	}

	POP_GLES2_DEBUG;
	return prog;

error:
	POP_GLES2_DEBUG;
	return 0;
}

extern const GLchar quad_vertex_src[];
extern const GLchar quad_fragment_src[];
extern const GLchar ellipse_fragment_src[];
extern const GLchar tex_vertex_src[];
extern const GLchar tex_fragment_src_rgba[];
extern const GLchar tex_fragment_src_rgbx[];
extern const GLchar tex_fragment_src_external[];

struct wlr_renderer *wlr_gles2_renderer_create(struct wlr_egl *egl) {
	if (!load_glapi()) {
		return NULL;
	}

	struct wlr_gles2_renderer *renderer =
		calloc(1, sizeof(struct wlr_gles2_renderer));
	if (renderer == NULL) {
		return NULL;
	}
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

	renderer->egl = egl;
	wlr_egl_make_current(renderer->egl, EGL_NO_SURFACE, NULL);

	renderer->exts_str = (const char*) glGetString(GL_EXTENSIONS);
	wlr_log(L_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(L_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
	wlr_log(L_INFO, "Supported GLES2 extensions: %s", renderer->exts_str);

	if (glDebugMessageCallbackKHR && glDebugMessageControlKHR) {
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		glDebugMessageCallbackKHR(gles2_log, NULL);

		// Silence unwanted message types
		glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP_KHR,
			GL_DONT_CARE, 0, NULL, GL_FALSE);
		glDebugMessageControlKHR(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP_KHR,
			GL_DONT_CARE, 0, NULL, GL_FALSE);
	}

	PUSH_GLES2_DEBUG;

	renderer->shaders.quad = link_program(quad_vertex_src, quad_fragment_src);
	if (!renderer->shaders.quad) {
		goto error;
	}
	renderer->shaders.ellipse =
		link_program(quad_vertex_src, ellipse_fragment_src);
	if (!renderer->shaders.ellipse) {
		goto error;
	}
	renderer->shaders.tex_rgba =
		link_program(tex_vertex_src, tex_fragment_src_rgba);
	if (!renderer->shaders.tex_rgba) {
		goto error;
	}
	renderer->shaders.tex_rgbx =
		link_program(tex_vertex_src, tex_fragment_src_rgbx);
	if (!renderer->shaders.tex_rgbx) {
		goto error;
	}
	if (glEGLImageTargetTexture2DOES) {
		renderer->shaders.tex_ext =
			link_program(tex_vertex_src, tex_fragment_src_external);
		if (!renderer->shaders.tex_ext) {
			goto error;
		}
	}

	POP_GLES2_DEBUG;

	return &renderer->wlr_renderer;

error:
	glDeleteProgram(renderer->shaders.quad);
	glDeleteProgram(renderer->shaders.ellipse);
	glDeleteProgram(renderer->shaders.tex_rgba);
	glDeleteProgram(renderer->shaders.tex_rgbx);
	glDeleteProgram(renderer->shaders.tex_ext);

	POP_GLES2_DEBUG;

	if (glDebugMessageCallbackKHR) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		glDebugMessageCallbackKHR(NULL, NULL);
	}

	free(renderer);
	return NULL;
}
