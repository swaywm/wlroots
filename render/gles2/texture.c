#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"

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
	GL_CALL(glGenTextures(1, &texture->tex_id));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride));
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
			fmt->gl_format, fmt->gl_type, pixels));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0));
	texture->wlr_texture->valid = true;
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

	GL_CALL(glGenTextures(1, &texture->tex_id));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, texture->tex_id));
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch));
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
				fmt->gl_format, fmt->gl_type, pixels));

	texture->wlr_texture->valid = true;
	wl_shm_buffer_end_access(buffer);
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
	.upload_shm = gles2_texture_upload_shm,
	.get_matrix = gles2_texture_get_matrix,
	.bind = gles2_texture_bind,
	.destroy = gles2_texture_destroy,
};

struct wlr_texture *gles2_texture_init() {
	struct wlr_texture_state *state = calloc(sizeof(struct wlr_texture_state), 1);
	struct wlr_texture *texture = wlr_texture_init(state, &wlr_texture_impl);
	state->wlr_texture = texture;
	wl_signal_init(&texture->destroy_signal);
	return texture;
}
