#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES3/gl3.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include "render/gles3.h"

static bool gles3_surface_attach_pixels(struct wlr_surface_state *surface,
		uint32_t format, int width, int height, const unsigned char *pixels) {
	assert(surface);
	surface->wlr_surface->width = width;
	surface->wlr_surface->height = height;
	surface->wlr_surface->format = format;
	// TODO: Error handling
	glGenTextures(1, &surface->tex_id);
	glBindTexture(GL_TEXTURE_2D, surface->tex_id);
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
			format, GL_UNSIGNED_BYTE, pixels);
	surface->wlr_surface->valid = true;
	return true;
}

static void gles3_surface_get_matrix(struct wlr_surface_state *surface,
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

static void gles3_surface_bind(struct wlr_surface_state *surface) {
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, surface->tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static void gles3_surface_destroy(struct wlr_surface_state *surface) {
	// TODO
}

static struct wlr_surface_impl wlr_surface_impl = {
	.attach_pixels = gles3_surface_attach_pixels,
	// .attach_shm = TODO
	.get_matrix = gles3_surface_get_matrix,
	.bind = gles3_surface_bind,
	.destroy = gles3_surface_destroy,
};

struct wlr_surface *gles3_surface_init() {
	struct wlr_surface_state *state = calloc(sizeof(struct wlr_surface_state), 1);
	struct wlr_surface *surface = wlr_surface_init(state, &wlr_surface_impl);
	state->wlr_surface = surface;
	return surface;
}
