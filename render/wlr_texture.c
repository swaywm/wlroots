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

bool wlr_texture_update_pixels(struct wlr_texture *texture,
		enum wl_shm_format format, int stride, int x, int y,
		int width, int height, const unsigned char *pixels) {
	return texture->impl->update_pixels(texture->state,
			format, stride, x, y, width, height, pixels);
}

bool wlr_texture_upload_shm(struct wlr_texture *texture, uint32_t format,
		struct wl_shm_buffer *shm) {
	return texture->impl->upload_shm(texture->state, format, shm);
}

bool wlr_texture_update_shm(struct wlr_texture *texture, uint32_t format,
		int x, int y, int width, int height, struct wl_shm_buffer *shm) {
	return texture->impl->update_shm(texture->state, format,
			x, y, width, height, shm);
}

bool wlr_texture_upload_drm(struct wlr_texture *texture,
		struct wl_resource *drm_buffer) {
	return texture->impl->upload_drm(texture->state, drm_buffer);
}

void wlr_texture_get_matrix(struct wlr_texture *texture,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	texture->impl->get_matrix(texture->state, matrix, projection, x, y);
}
