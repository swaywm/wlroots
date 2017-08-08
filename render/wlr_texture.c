#include <stdlib.h>
#include <stdbool.h>
#include <wlr/render/interface.h>

struct wlr_texture *wlr_texture_init(struct wlr_texture_state *state,
		struct wlr_texture_impl *impl) {
	struct wlr_texture *t = calloc(sizeof(struct wlr_texture), 1);
	t->state = state;
	t->impl = impl;
	return t;
}

void wlr_texture_destroy(struct wlr_texture *texture) {
	texture->impl->destroy(texture->state);
	free(texture);
}

void wlr_texture_bind(struct wlr_texture *texture) {
	texture->impl->bind(texture->state);
}

bool wlr_texture_upload_pixels(struct wlr_texture *texture, uint32_t format,
		int stride, int width, int height, const unsigned char *pixels) {
	return texture->impl->upload_pixels(texture->state,
			format, stride, width, height, pixels);
}

bool wlr_texture_upload_shm(struct wlr_texture *texture, uint32_t format,
		struct wl_shm_buffer *shm) {
	return texture->impl->upload_shm(texture->state, format, shm);
}

void wlr_texture_get_matrix(struct wlr_texture *texture,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	texture->impl->get_matrix(texture->state, matrix, projection, x, y);
}
