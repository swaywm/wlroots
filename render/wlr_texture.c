#include <stdlib.h>
#include <stdbool.h>
#include <wlr/render/interface.h>

void wlr_texture_init(struct wlr_texture *texture,
		struct wlr_texture_impl *impl) {
	texture->impl = impl;
	wl_signal_init(&texture->destroy_signal);
}

void wlr_texture_destroy(struct wlr_texture *texture) {
	if (texture && texture->impl && texture->impl->destroy) {
		texture->impl->destroy(texture);
	} else {
		free(texture);
	}
}

void wlr_texture_bind(struct wlr_texture *texture) {
	texture->impl->bind(texture);
}

bool wlr_texture_upload_pixels(struct wlr_texture *texture, uint32_t format,
		int stride, int width, int height, const unsigned char *pixels) {
	return texture->impl->upload_pixels(texture, format, stride,
			width, height, pixels);
}

bool wlr_texture_update_pixels(struct wlr_texture *texture,
		enum wl_shm_format format, int stride, int x, int y,
		int width, int height, const unsigned char *pixels) {
	return texture->impl->update_pixels(texture, format, stride, x, y,
			width, height, pixels);
}

bool wlr_texture_upload_shm(struct wlr_texture *texture, uint32_t format,
		struct wl_shm_buffer *shm) {
	return texture->impl->upload_shm(texture, format, shm);
}

bool wlr_texture_update_shm(struct wlr_texture *texture, uint32_t format,
		int x, int y, int width, int height, struct wl_shm_buffer *shm) {
	return texture->impl->update_shm(texture, format, x, y, width, height, shm);
}

bool wlr_texture_upload_drm(struct wlr_texture *texture,
		struct wl_resource *drm_buffer) {
	return texture->impl->upload_drm(texture, drm_buffer);
}

void wlr_texture_get_matrix(struct wlr_texture *texture,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	texture->impl->get_matrix(texture, matrix, projection, x, y);
}

void wlr_texture_get_buffer_size(struct wlr_texture *texture, struct wl_resource
		*resource, int *width, int *height) {
	texture->impl->get_buffer_size(texture, resource, width, height);
}
