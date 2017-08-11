#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/egl.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

static struct pixel_format external_pixel_format = {
	.wl_format = 0,
	.depth = 0,
	.bpp = 0,
	.gl_format = 0,
	.gl_type = 0,
	.shader = &shaders.external
};

static void gles2_texture_ensure_texture(struct wlr_texture_state *surface) {
	if (surface->tex_id) {
		return;
	}
	GL_CALL(glGenTextures(1, &surface->tex_id));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
}

static bool gles2_texture_upload_pixels(struct wlr_texture_state *texture,
		enum wl_shm_format format, int stride, int width, int height,
		const unsigned char *pixels) {
	assert(texture);
	const struct pixel_format *fmt = gl_format_for_wl_format(format);
	if (!fmt || !fmt->gl_format) {
		wlr_log(L_ERROR, "No supported pixel format for this texture");
		return false;
	}
	texture->wlr_texture->width = width;
	texture->wlr_texture->height = height;
	texture->wlr_texture->format = format;
	texture->pixel_format = fmt;

	gles2_texture_ensure_texture(texture);
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride));
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
			fmt->gl_format, fmt->gl_type, pixels));
	texture->wlr_texture->valid = true;
	return true;
}

static bool gles2_texture_update_pixels(struct wlr_texture_state *texture,
		enum wl_shm_format format, int stride, int x, int y,
		int width, int height, const unsigned char *pixels) {
	assert(texture && texture->wlr_texture->valid);
	// TODO: Test if the unpack subimage extension is supported and adjust the
	// upload strategy if not
	if (!texture->wlr_texture->valid
			|| texture->wlr_texture->format != format
		/*	|| unpack not supported */) {
		return gles2_texture_upload_pixels(texture, format, stride,
				width, height, pixels);
	}
	const struct pixel_format *fmt = texture->pixel_format;
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, x));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, y));
	GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height,
			fmt->gl_format, fmt->gl_type, pixels));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0));
	return true;
}

static bool gles2_texture_upload_shm(struct wlr_texture_state *texture,
		uint32_t format, struct wl_shm_buffer *buffer) {
	const struct pixel_format *fmt = gl_format_for_wl_format(format);
	if (!fmt || !fmt->gl_format) {
		wlr_log(L_ERROR, "No supported pixel format for this texture");
		return false;
	}
	wl_shm_buffer_begin_access(buffer);
	uint8_t *pixels = wl_shm_buffer_get_data(buffer);
	int width = wl_shm_buffer_get_width(buffer);
	int height = wl_shm_buffer_get_height(buffer);
	int pitch = wl_shm_buffer_get_stride(buffer) / (fmt->bpp / 8);
	texture->wlr_texture->width = width;
	texture->wlr_texture->height = height;
	texture->wlr_texture->format = format;
	texture->pixel_format = fmt;

	gles2_texture_ensure_texture(texture);
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0));
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
				fmt->gl_format, fmt->gl_type, pixels));

	texture->wlr_texture->valid = true;
	wl_shm_buffer_end_access(buffer);
	return true;
}

static bool gles2_texture_update_shm(struct wlr_texture_state *texture,
		uint32_t format, int x, int y, int width, int height,
		struct wl_shm_buffer *buffer) {
	// TODO: Test if the unpack subimage extension is supported and adjust the
	// upload strategy if not
	assert(texture);
	if (!texture->wlr_texture->valid
			|| texture->wlr_texture->format != format
		/*	|| unpack not supported */) {
		return gles2_texture_upload_shm(texture, format, buffer);
	}
	const struct pixel_format *fmt = texture->pixel_format;
	wl_shm_buffer_begin_access(buffer);
	uint8_t *pixels = wl_shm_buffer_get_data(buffer);
	int pitch = wl_shm_buffer_get_stride(buffer) / (fmt->bpp / 8);

	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, x));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, y));
	GL_CALL(glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height,
			fmt->gl_format, fmt->gl_type, pixels));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0));
	GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0));

	wl_shm_buffer_end_access(buffer);

	return true;
}

static bool gles2_texture_upload_drm(struct wlr_texture_state *tex,
		struct wl_resource *buf) {
	if (!glEGLImageTargetTexture2DOES) {
		return false;
	}

	EGLint format;
	if (!wlr_egl_query_buffer(tex->egl, buf, EGL_TEXTURE_FORMAT, &format)) {
		wlr_log(L_INFO, "upload_drm called with no drm buffer");
		return false;
	}

	wlr_egl_query_buffer(tex->egl, buf, EGL_WIDTH,
			(EGLint*)&tex->wlr_texture->width);
	wlr_egl_query_buffer(tex->egl, buf, EGL_HEIGHT,
			(EGLint*)&tex->wlr_texture->height);

	EGLint inverted_y;
	wlr_egl_query_buffer(tex->egl, buf, EGL_WAYLAND_Y_INVERTED_WL, &inverted_y);

	GLenum target;
	const struct pixel_format *pf;
	switch (format) {
	case EGL_TEXTURE_RGB:
	case EGL_TEXTURE_RGBA:
		target = GL_TEXTURE_2D;
		pf = gl_format_for_wl_format(WL_SHM_FORMAT_ARGB8888);
		break;
	case EGL_TEXTURE_EXTERNAL_WL:
		target = GL_TEXTURE_EXTERNAL_OES;
		pf = &external_pixel_format;
		break;
	default:
		wlr_log(L_ERROR, "invalid/unsupported egl buffer format");
		return false;
	}

	gles2_texture_ensure_texture(tex);
	GL_CALL(glBindTexture(GL_TEXTURE_2D, tex->tex_id));

	EGLint attribs[] = { EGL_WAYLAND_PLANE_WL, 0, EGL_NONE };
	tex->image = wlr_egl_create_image(tex->egl, EGL_WAYLAND_BUFFER_WL,
		(EGLClientBuffer*) buf, attribs);
	if (!tex->image) {
		wlr_log(L_ERROR, "failed to create egl image: %s", egl_error());
 		return false;
	}

	GL_CALL(glActiveTexture(GL_TEXTURE0));
	GL_CALL(glBindTexture(target, tex->tex_id));
	GL_CALL(glEGLImageTargetTexture2DOES(target, tex->image));
	tex->wlr_texture->valid = true;
	tex->pixel_format = pf;

	return true;
}

static void gles2_texture_get_matrix(struct wlr_texture_state *texture,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	struct wlr_texture *_texture = texture->wlr_texture;
	float world[16];
	wlr_matrix_identity(matrix);
	wlr_matrix_translate(&world, x, y, 0);
	wlr_matrix_mul(matrix, &world, matrix);
	wlr_matrix_scale(&world, _texture->width, _texture->height, 1);
	wlr_matrix_mul(matrix, &world, matrix);
	wlr_matrix_mul(projection, matrix, matrix);
}

static void gles2_texture_bind(struct wlr_texture_state *texture) {
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL_CALL(glUseProgram(*texture->pixel_format->shader));
}

static void gles2_texture_destroy(struct wlr_texture_state *texture) {
	wl_signal_emit(&texture->wlr_texture->destroy_signal, texture->wlr_texture);
	GL_CALL(glDeleteTextures(1, &texture->tex_id));
	free(texture);
}

static struct wlr_texture_impl wlr_texture_impl = {
	.upload_pixels = gles2_texture_upload_pixels,
	.update_pixels = gles2_texture_update_pixels,
	.upload_shm = gles2_texture_upload_shm,
	.update_shm = gles2_texture_update_shm,
	.upload_drm = gles2_texture_upload_drm,
	.get_matrix = gles2_texture_get_matrix,
	.bind = gles2_texture_bind,
	.destroy = gles2_texture_destroy,
};

struct wlr_texture *gles2_texture_init(struct wlr_egl *egl) {
	struct wlr_texture_state *state = calloc(sizeof(struct wlr_texture_state), 1);
	struct wlr_texture *texture = wlr_texture_init(state, &wlr_texture_impl);
	state->wlr_texture = texture;
	state->egl = egl;
	wl_signal_init(&texture->destroy_signal);
	return texture;
}
