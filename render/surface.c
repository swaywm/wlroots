#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES3/gl3.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/matrix.h>
#include "render.h"

struct wlr_surface *wlr_surface_init() {
	return calloc(sizeof(struct wlr_surface), 1);
}

void wlr_surface_attach_pixels(struct wlr_surface *surf, uint32_t format,
		int width, int height, const unsigned char *pixels) {
	assert(surf);
	surf->width = width;
	surf->height = height;
	surf->format = format;
	// TODO: Error handling
	glGenTextures(1, &surf->tex_id);
	glBindTexture(GL_TEXTURE_2D, surf->tex_id);
	glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
			format, GL_UNSIGNED_BYTE, pixels);
	surf->valid = true;
}

void wlr_surface_attach_shm(struct wlr_surface *surf, uint32_t format,
		struct wl_shm_buffer *shm);

void wlr_surface_destroy(struct wlr_surface *tex) {
	// TODO
}
