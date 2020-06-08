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
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

static const GLfloat verts[] = {
	1, 0, // top right
	0, 0, // top left
	1, 1, // bottom right
	0, 1, // bottom left
};

struct wlr_gles2_procs gles2_procs = {0};

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
		wlr_box_transform(&gl_box, box, WL_OUTPUT_TRANSFORM_FLIPPED_180,
			renderer->viewport_width, renderer->viewport_height);

		glScissor(gl_box.x, gl_box.y, gl_box.width, gl_box.height);
		glEnable(GL_SCISSOR_TEST);
	} else {
		glDisable(GL_SCISSOR_TEST);
	}
	POP_GLES2_DEBUG;
}

static bool gles2_render_subtexture_with_matrix(
		struct wlr_renderer *wlr_renderer, struct wlr_texture *wlr_texture,
		const struct wlr_fbox *box, const float matrix[static 9],
		float alpha) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);
	struct wlr_gles2_texture *texture =
		gles2_get_texture(wlr_texture);

	struct wlr_gles2_tex_shader *shader = NULL;

	switch (texture->target) {
	case GL_TEXTURE_2D:
		if (texture->has_alpha) {
			shader = &renderer->shaders.tex_rgba;
		} else {
			shader = &renderer->shaders.tex_rgbx;
		}
		break;
	case GL_TEXTURE_EXTERNAL_OES:
		shader = &renderer->shaders.tex_ext;

		if (!renderer->exts.egl_image_external_oes) {
			wlr_log(WLR_ERROR, "Failed to render texture: "
				"GL_TEXTURE_EXTERNAL_OES not supported");
			return false;
		}
		break;
	default:
		abort();
	}

	// OpenGL ES 2 requires the glUniformMatrix3fv transpose parameter to be set
	// to GL_FALSE
	float transposition[9];
	wlr_matrix_transpose(transposition, matrix);

	PUSH_GLES2_DEBUG;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(texture->target, texture->tex);

	glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glUseProgram(shader->program);

	glUniformMatrix3fv(shader->proj, 1, GL_FALSE, transposition);
	glUniform1i(shader->invert_y, texture->inverted_y);
	glUniform1i(shader->tex, 0);
	glUniform1f(shader->alpha, alpha);

	const GLfloat x1 = box->x / wlr_texture->width;
	const GLfloat y1 = box->y / wlr_texture->height;
	const GLfloat x2 = (box->x + box->width) / wlr_texture->width;
	const GLfloat y2 = (box->y + box->height) / wlr_texture->height;
	const GLfloat texcoord[] = {
		x2, y1, // top right
		x1, y1, // top left
		x2, y2, // bottom right
		x1, y2, // bottom left
	};

	glVertexAttribPointer(shader->pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(shader->tex_attrib, 2, GL_FLOAT, GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(shader->pos_attrib);
	glEnableVertexAttribArray(shader->tex_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(shader->pos_attrib);
	glDisableVertexAttribArray(shader->tex_attrib);

	glBindTexture(texture->target, 0);

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
	glUseProgram(renderer->shaders.quad.program);

	glUniformMatrix3fv(renderer->shaders.quad.proj, 1, GL_FALSE, transposition);
	glUniform4f(renderer->shaders.quad.color, color[0], color[1], color[2], color[3]);

	glVertexAttribPointer(renderer->shaders.quad.pos_attrib, 2, GL_FLOAT, GL_FALSE,
			0, verts);

	glEnableVertexAttribArray(renderer->shaders.quad.pos_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(renderer->shaders.quad.pos_attrib);

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

	static const GLfloat texcoord[] = {
		1, 0, // top right
		0, 0, // top left
		1, 1, // bottom right
		0, 1, // bottom left
	};

	PUSH_GLES2_DEBUG;
	glUseProgram(renderer->shaders.ellipse.program);

	glUniformMatrix3fv(renderer->shaders.ellipse.proj, 1, GL_FALSE, transposition);
	glUniform4f(renderer->shaders.ellipse.color, color[0], color[1], color[2], color[3]);

	glVertexAttribPointer(renderer->shaders.ellipse.pos_attrib, 2, GL_FLOAT,
			GL_FALSE, 0, verts);
	glVertexAttribPointer(renderer->shaders.ellipse.tex_attrib, 2, GL_FLOAT,
			GL_FALSE, 0, texcoord);

	glEnableVertexAttribArray(renderer->shaders.ellipse.pos_attrib);
	glEnableVertexAttribArray(renderer->shaders.ellipse.tex_attrib);

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableVertexAttribArray(renderer->shaders.ellipse.pos_attrib);
	glDisableVertexAttribArray(renderer->shaders.ellipse.tex_attrib);
	POP_GLES2_DEBUG;
}

static const enum wl_shm_format *gles2_renderer_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	return get_gles2_wl_formats(len);
}

static bool gles2_format_supported(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt) {
	return get_gles2_format_from_wl(wl_fmt) != NULL;
}

static bool gles2_resource_is_wl_drm_buffer(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	if (!renderer->egl->exts.bind_wayland_display_wl) {
		return false;
	}

	EGLint fmt;
	return renderer->egl->procs.eglQueryWaylandBufferWL(renderer->egl->display,
		resource, EGL_TEXTURE_FORMAT, &fmt);
}

static void gles2_wl_drm_buffer_get_size(struct wlr_renderer *wlr_renderer,
		struct wl_resource *buffer, int *width, int *height) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer(wlr_renderer);

	if (!renderer->egl->exts.bind_wayland_display_wl) {
		return;
	}

	renderer->egl->procs.eglQueryWaylandBufferWL(renderer->egl->display,
		buffer, EGL_WIDTH, width);
	renderer->egl->procs.eglQueryWaylandBufferWL(renderer->egl->display,
		buffer, EGL_HEIGHT, height);
}

static const struct wlr_drm_format_set *gles2_get_dmabuf_formats(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return wlr_egl_get_dmabuf_formats(renderer->egl);
}

static enum wl_shm_format gles2_preferred_read_format(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	GLint gl_format = -1, gl_type = -1;
	PUSH_GLES2_DEBUG;
	glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &gl_format);
	glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &gl_type);
	POP_GLES2_DEBUG;

	EGLint alpha_size = -1;
	eglGetConfigAttrib(renderer->egl->display, renderer->egl->config,
		EGL_ALPHA_SIZE, &alpha_size);

	const struct wlr_gles2_pixel_format *fmt =
		get_gles2_format_from_gl(gl_format, gl_type, alpha_size > 0);
	if (fmt != NULL) {
		return fmt->wl_format;
	}

	if (renderer->exts.read_format_bgra_ext) {
		return WL_SHM_FORMAT_XRGB8888;
	}
	return WL_SHM_FORMAT_XBGR8888;
}

static bool gles2_read_pixels(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt, uint32_t *flags, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	const struct wlr_gles2_pixel_format *fmt = get_gles2_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(WLR_ERROR, "Cannot read pixels: unsupported pixel format");
		return false;
	}

	if (fmt->gl_format == GL_BGRA_EXT && !renderer->exts.read_format_bgra_ext) {
		wlr_log(WLR_ERROR,
			"Cannot read pixels: missing GL_EXT_read_format_bgra extension");
		return false;
	}

	PUSH_GLES2_DEBUG;

	// Make sure any pending drawing is finished before we try to read it
	glFinish();

	glGetError(); // Clear the error flag

	unsigned char *p = (unsigned char *)data + dst_y * stride;
	uint32_t pack_stride = width * fmt->bpp / 8;
	if (pack_stride == stride && dst_x == 0 && flags != NULL) {
		// Under these particular conditions, we can read the pixels with only
		// one glReadPixels call
		glReadPixels(src_x, renderer->viewport_height - height - src_y,
			width, height, fmt->gl_format, fmt->gl_type, p);
		*flags = WLR_RENDERER_READ_PIXELS_Y_INVERT;
	} else {
		// Unfortunately GLES2 doesn't support GL_PACK_*, so we have to read
		// the lines out row by row
		for (size_t i = 0; i < height; ++i) {
			glReadPixels(src_x, renderer->viewport_height - src_y - i - 1, width, 1, fmt->gl_format,
				fmt->gl_type, p + i * stride + dst_x * fmt->bpp / 8);
		}
		if (flags != NULL) {
			*flags = 0;
		}
	}

	POP_GLES2_DEBUG;

	return glGetError() == GL_NO_ERROR;
}

static bool gles2_blit_dmabuf(struct wlr_renderer *wlr_renderer,
		struct wlr_dmabuf_attributes *dst_attr,
		struct wlr_dmabuf_attributes *src_attr) {
	if (!gles2_procs.glEGLImageTargetRenderbufferStorageOES) {
		return false;
	}

	struct wlr_egl_context old_context;
	wlr_egl_save_context(&old_context);

	bool r = false;
	struct wlr_texture *src_tex =
		wlr_texture_from_dmabuf(wlr_renderer, src_attr);
	if (!src_tex) {
		goto restore_context_out;
	}

	// This is to take into account y-inversion on both buffers rather than
	// just the source buffer.
	bool src_inverted_y =
		!!(src_attr->flags & WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT);
	bool dst_inverted_y =
		!!(dst_attr->flags & WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT);
	struct wlr_gles2_texture *gles2_src_tex = gles2_get_texture(src_tex);
	gles2_src_tex->inverted_y = src_inverted_y ^ dst_inverted_y;

	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(wlr_renderer);
	if (!wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL)) {
		goto texture_destroy_out;
	}

	// TODO: The imported buffer should be checked with
	// eglQueryDmaBufModifiersEXT to see if it may be modified.
	bool external_only = false;
	EGLImageKHR image = wlr_egl_create_image_from_dmabuf(egl, dst_attr,
			&external_only);
	if (image == EGL_NO_IMAGE_KHR) {
		goto texture_destroy_out;
	}

	GLuint rbo = 0;
	glGenRenderbuffers(1, &rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, rbo);
	gles2_procs.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER,
			image);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			GL_RENDERBUFFER, rbo);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		goto out;
	}

	// TODO: use ANGLE_framebuffer_blit if available
	float mat[9];
	wlr_matrix_projection(mat, 1, 1, WL_OUTPUT_TRANSFORM_NORMAL);

	wlr_renderer_begin(wlr_renderer, dst_attr->width, dst_attr->height);
	wlr_renderer_clear(wlr_renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(wlr_renderer, src_tex, mat, 1.0f);
	wlr_renderer_end(wlr_renderer);

	r = true;
out:
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteRenderbuffers(1, &rbo);
	wlr_egl_destroy_image(egl, image);
texture_destroy_out:
	wlr_texture_destroy(src_tex);
restore_context_out:
	wlr_egl_restore_context(&old_context);
	return r;
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

static bool gles2_init_wl_display(struct wlr_renderer *wlr_renderer,
		struct wl_display *wl_display) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer(wlr_renderer);

	if (renderer->egl->exts.bind_wayland_display_wl) {
		if (!wlr_egl_bind_display(renderer->egl, wl_display)) {
			wlr_log(WLR_ERROR, "Failed to bind wl_display to EGL");
			return false;
		}
	} else {
		wlr_log(WLR_INFO, "EGL_WL_bind_wayland_display is not supported");
	}

	if (renderer->egl->exts.image_dmabuf_import_ext) {
		if (wlr_linux_dmabuf_v1_create(wl_display, wlr_renderer) == NULL) {
			return false;
		}
	} else {
		wlr_log(WLR_INFO, "EGL_EXT_image_dma_buf_import is not supported");
	}

	return true;
}

struct wlr_egl *wlr_gles2_renderer_get_egl(struct wlr_renderer *wlr_renderer) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer(wlr_renderer);
	return renderer->egl;
}

static void gles2_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	wlr_egl_make_current(renderer->egl, EGL_NO_SURFACE, NULL);

	PUSH_GLES2_DEBUG;
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.ellipse.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);
	POP_GLES2_DEBUG;

	if (renderer->exts.debug_khr) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		gles2_procs.glDebugMessageCallbackKHR(NULL, NULL);
	}

	wlr_egl_unset_current(renderer->egl);

	free(renderer);
}

static const struct wlr_renderer_impl renderer_impl = {
	.destroy = gles2_destroy,
	.begin = gles2_begin,
	.end = gles2_end,
	.clear = gles2_clear,
	.scissor = gles2_scissor,
	.render_subtexture_with_matrix = gles2_render_subtexture_with_matrix,
	.render_quad_with_matrix = gles2_render_quad_with_matrix,
	.render_ellipse_with_matrix = gles2_render_ellipse_with_matrix,
	.formats = gles2_renderer_formats,
	.format_supported = gles2_format_supported,
	.resource_is_wl_drm_buffer = gles2_resource_is_wl_drm_buffer,
	.wl_drm_buffer_get_size = gles2_wl_drm_buffer_get_size,
	.get_dmabuf_formats = gles2_get_dmabuf_formats,
	.preferred_read_format = gles2_preferred_read_format,
	.read_pixels = gles2_read_pixels,
	.texture_from_pixels = gles2_texture_from_pixels,
	.texture_from_wl_drm = gles2_texture_from_wl_drm,
	.texture_from_dmabuf = gles2_texture_from_dmabuf,
	.init_wl_display = gles2_init_wl_display,
	.blit_dmabuf = gles2_blit_dmabuf,
};

void push_gles2_marker(const char *file, const char *func) {
	if (!gles2_procs.glPushDebugGroupKHR) {
		return;
	}

	int len = snprintf(NULL, 0, "%s:%s", file, func) + 1;
	char str[len];
	snprintf(str, len, "%s:%s", file, func);
	gles2_procs.glPushDebugGroupKHR(GL_DEBUG_SOURCE_APPLICATION_KHR, 1, -1, str);
}

void pop_gles2_marker(void) {
	if (gles2_procs.glPopDebugGroupKHR) {
		gles2_procs.glPopDebugGroupKHR();
	}
}

static enum wlr_log_importance gles2_log_importance_to_wlr(GLenum type) {
	switch (type) {
	case GL_DEBUG_TYPE_ERROR_KHR:               return WLR_ERROR;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR: return WLR_DEBUG;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR:  return WLR_ERROR;
	case GL_DEBUG_TYPE_PORTABILITY_KHR:         return WLR_DEBUG;
	case GL_DEBUG_TYPE_PERFORMANCE_KHR:         return WLR_DEBUG;
	case GL_DEBUG_TYPE_OTHER_KHR:               return WLR_DEBUG;
	case GL_DEBUG_TYPE_MARKER_KHR:              return WLR_DEBUG;
	case GL_DEBUG_TYPE_PUSH_GROUP_KHR:          return WLR_DEBUG;
	case GL_DEBUG_TYPE_POP_GROUP_KHR:           return WLR_DEBUG;
	default:                                    return WLR_DEBUG;
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

static bool check_gl_ext(const char *exts, const char *ext) {
	size_t extlen = strlen(ext);
	const char *end = exts + strlen(exts);

	while (exts < end) {
		if (exts[0] == ' ') {
			exts++;
			continue;
		}
		size_t n = strcspn(exts, " ");
		if (n == extlen && strncmp(ext, exts, n) == 0) {
			return true;
		}
		exts += n;
	}
	return false;
}

static void load_gl_proc(void *proc_ptr, const char *name) {
	void *proc = (void *)eglGetProcAddress(name);
	if (proc == NULL) {
		wlr_log(WLR_ERROR, "eglGetProcAddress(%s) failed", name);
		abort();
	}
	*(void **)proc_ptr = proc;
}

extern const GLchar quad_vertex_src[];
extern const GLchar quad_fragment_src[];
extern const GLchar ellipse_fragment_src[];
extern const GLchar tex_vertex_src[];
extern const GLchar tex_fragment_src_rgba[];
extern const GLchar tex_fragment_src_rgbx[];
extern const GLchar tex_fragment_src_external[];

struct wlr_renderer *wlr_gles2_renderer_create(struct wlr_egl *egl) {
	if (!wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL)) {
		return NULL;
	}

	const char *exts_str = (const char *)glGetString(GL_EXTENSIONS);
	if (exts_str == NULL) {
		wlr_log(WLR_ERROR, "Failed to get GL_EXTENSIONS");
		return NULL;
	}

	struct wlr_gles2_renderer *renderer =
		calloc(1, sizeof(struct wlr_gles2_renderer));
	if (renderer == NULL) {
		return NULL;
	}
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

	renderer->egl = egl;
	renderer->exts_str = exts_str;

	wlr_log(WLR_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(WLR_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
	wlr_log(WLR_INFO, "GL renderer: %s", glGetString(GL_RENDERER));
	wlr_log(WLR_INFO, "Supported GLES2 extensions: %s", exts_str);

	if (!check_gl_ext(exts_str, "GL_EXT_texture_format_BGRA8888")) {
		wlr_log(WLR_ERROR, "BGRA8888 format not supported by GLES2");
		free(renderer);
		return NULL;
	}

	renderer->exts.read_format_bgra_ext =
		check_gl_ext(exts_str, "GL_EXT_read_format_bgra");

	if (check_gl_ext(exts_str, "GL_KHR_debug")) {
		renderer->exts.debug_khr = true;
		load_gl_proc(&gles2_procs.glDebugMessageCallbackKHR,
			"glDebugMessageCallbackKHR");
		load_gl_proc(&gles2_procs.glDebugMessageControlKHR,
			"glDebugMessageControlKHR");
	}

	if (check_gl_ext(exts_str, "GL_OES_EGL_image_external")) {
		renderer->exts.egl_image_external_oes = true;
		load_gl_proc(&gles2_procs.glEGLImageTargetTexture2DOES,
			"glEGLImageTargetTexture2DOES");
	}

	if (check_gl_ext(exts_str, "GL_OES_EGL_image")) {
		renderer->exts.egl_image_oes = true;
		load_gl_proc(&gles2_procs.glEGLImageTargetRenderbufferStorageOES,
			"glEGLImageTargetRenderbufferStorageOES");
	}

	if (renderer->exts.debug_khr) {
		glEnable(GL_DEBUG_OUTPUT_KHR);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
		gles2_procs.glDebugMessageCallbackKHR(gles2_log, NULL);

		// Silence unwanted message types
		gles2_procs.glDebugMessageControlKHR(GL_DONT_CARE,
			GL_DEBUG_TYPE_POP_GROUP_KHR, GL_DONT_CARE, 0, NULL, GL_FALSE);
		gles2_procs.glDebugMessageControlKHR(GL_DONT_CARE,
			GL_DEBUG_TYPE_PUSH_GROUP_KHR, GL_DONT_CARE, 0, NULL, GL_FALSE);
	}

	PUSH_GLES2_DEBUG;

	GLuint prog;
	renderer->shaders.quad.program = prog =
		link_program(quad_vertex_src, quad_fragment_src);
	if (!renderer->shaders.quad.program) {
		goto error;
	}
	renderer->shaders.quad.proj = glGetUniformLocation(prog, "proj");
	renderer->shaders.quad.color = glGetUniformLocation(prog, "color");
	renderer->shaders.quad.pos_attrib = glGetAttribLocation(prog, "pos");

	renderer->shaders.ellipse.program = prog =
		link_program(quad_vertex_src, ellipse_fragment_src);
	if (!renderer->shaders.ellipse.program) {
		goto error;
	}
	renderer->shaders.ellipse.proj = glGetUniformLocation(prog, "proj");
	renderer->shaders.ellipse.color = glGetUniformLocation(prog, "color");
	renderer->shaders.ellipse.pos_attrib = glGetAttribLocation(prog, "pos");
	renderer->shaders.ellipse.tex_attrib = glGetAttribLocation(prog, "texcoord");

	renderer->shaders.tex_rgba.program = prog =
		link_program(tex_vertex_src, tex_fragment_src_rgba);
	if (!renderer->shaders.tex_rgba.program) {
		goto error;
	}
	renderer->shaders.tex_rgba.proj = glGetUniformLocation(prog, "proj");
	renderer->shaders.tex_rgba.invert_y = glGetUniformLocation(prog, "invert_y");
	renderer->shaders.tex_rgba.tex = glGetUniformLocation(prog, "tex");
	renderer->shaders.tex_rgba.alpha = glGetUniformLocation(prog, "alpha");
	renderer->shaders.tex_rgba.pos_attrib = glGetAttribLocation(prog, "pos");
	renderer->shaders.tex_rgba.tex_attrib = glGetAttribLocation(prog, "texcoord");

	renderer->shaders.tex_rgbx.program = prog =
		link_program(tex_vertex_src, tex_fragment_src_rgbx);
	if (!renderer->shaders.tex_rgbx.program) {
		goto error;
	}
	renderer->shaders.tex_rgbx.proj = glGetUniformLocation(prog, "proj");
	renderer->shaders.tex_rgbx.invert_y = glGetUniformLocation(prog, "invert_y");
	renderer->shaders.tex_rgbx.tex = glGetUniformLocation(prog, "tex");
	renderer->shaders.tex_rgbx.alpha = glGetUniformLocation(prog, "alpha");
	renderer->shaders.tex_rgbx.pos_attrib = glGetAttribLocation(prog, "pos");
	renderer->shaders.tex_rgbx.tex_attrib = glGetAttribLocation(prog, "texcoord");

	if (renderer->exts.egl_image_external_oes) {
		renderer->shaders.tex_ext.program = prog =
			link_program(tex_vertex_src, tex_fragment_src_external);
		if (!renderer->shaders.tex_ext.program) {
			goto error;
		}
		renderer->shaders.tex_ext.proj = glGetUniformLocation(prog, "proj");
		renderer->shaders.tex_ext.invert_y = glGetUniformLocation(prog, "invert_y");
		renderer->shaders.tex_ext.tex = glGetUniformLocation(prog, "tex");
		renderer->shaders.tex_ext.alpha = glGetUniformLocation(prog, "alpha");
		renderer->shaders.tex_ext.pos_attrib = glGetAttribLocation(prog, "pos");
		renderer->shaders.tex_ext.tex_attrib = glGetAttribLocation(prog, "texcoord");
	}

	POP_GLES2_DEBUG;

	wlr_egl_unset_current(renderer->egl);

	return &renderer->wlr_renderer;

error:
	glDeleteProgram(renderer->shaders.quad.program);
	glDeleteProgram(renderer->shaders.ellipse.program);
	glDeleteProgram(renderer->shaders.tex_rgba.program);
	glDeleteProgram(renderer->shaders.tex_rgbx.program);
	glDeleteProgram(renderer->shaders.tex_ext.program);

	POP_GLES2_DEBUG;

	if (renderer->exts.debug_khr) {
		glDisable(GL_DEBUG_OUTPUT_KHR);
		gles2_procs.glDebugMessageCallbackKHR(NULL, NULL);
	}

	wlr_egl_unset_current(renderer->egl);

	free(renderer);
	return NULL;
}

bool wlr_gles2_renderer_check_ext(struct wlr_renderer *wlr_renderer,
		const char *ext) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	return check_gl_ext(renderer->exts_str, ext);
}
