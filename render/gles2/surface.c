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

static bool gles2_surface_attach_pixels(struct wlr_surface_state *surface,
		enum wl_shm_format format, int width, int height,
		const unsigned char *pixels) {
	assert(surface);
	const struct pixel_format *fmt = gl_format_for_wl_format(format);
	if (!fmt || !fmt->gl_format) {
		wlr_log(L_ERROR, "No supported pixel format for this surface");
		return false;
	}
	surface->wlr_surface->width = width;
	surface->wlr_surface->height = height;
	surface->wlr_surface->format = format;
	surface->pixel_format = fmt;
	GL_CALL(glGenTextures(1, &surface->tex_id));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
			fmt->gl_format, fmt->gl_type, pixels));
	surface->wlr_surface->valid = true;
	return true;
}

static void gles2_surface_get_matrix(struct wlr_surface_state *surface,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	struct wlr_surface *_surface = surface->wlr_surface;
	float world[16];
	wlr_matrix_identity(matrix);
	wlr_matrix_translate(&world, x, y, 0);
	wlr_matrix_mul(matrix, &world, matrix);
	wlr_matrix_scale(&world, _surface->width, _surface->height, 1);
	wlr_matrix_mul(matrix, &world, matrix);
	wlr_matrix_mul(projection, matrix, matrix);
}

static void gles2_surface_bind(struct wlr_surface_state *surface) {
	GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL_CALL(glUseProgram(*surface->pixel_format->shader));
}

static void gles2_surface_destroy(struct wlr_surface_state *surface) {
	GL_CALL(glDeleteTextures(1, &surface->tex_id));
	free(surface);
}

static struct wlr_surface_impl wlr_surface_impl = {
	.attach_pixels = gles2_surface_attach_pixels,
	// .attach_shm = TODO
	.get_matrix = gles2_surface_get_matrix,
	.bind = gles2_surface_bind,
	.destroy = gles2_surface_destroy,
};

struct wlr_surface *gles2_surface_init() {
	struct wlr_surface_state *state = calloc(sizeof(struct wlr_surface_state), 1);
	struct wlr_surface *surface = wlr_surface_init(state, &wlr_surface_impl);
	state->wlr_surface = surface;
	return surface;
}
