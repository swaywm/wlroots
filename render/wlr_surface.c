#include <stdlib.h>
#include <stdbool.h>
#include <wlr/render/interface.h>

struct wlr_surface *wlr_surface_init(struct wlr_surface_state *state,
		struct wlr_surface_impl *impl) {
	struct wlr_surface *s = calloc(sizeof(struct wlr_surface), 1);
	s->state = state;
	s->impl = impl;
	return s;
}

void wlr_surface_destroy(struct wlr_surface *surface) {
	surface->impl->destroy(surface->state);
	free(surface);
}

void wlr_surface_bind(struct wlr_surface *surface) {
	surface->impl->bind(surface->state);
}

bool wlr_surface_attach_pixels(struct wlr_surface *surface, uint32_t format,
		int stride, int width, int height, const unsigned char *pixels) {
	return surface->impl->attach_pixels(surface->state,
			format, stride, width, height, pixels);
}

bool wlr_surface_attach_shm(struct wlr_surface *surface, uint32_t format,
		struct wl_shm_buffer *shm) {
	return surface->impl->attach_shm(surface->state, format, shm);
}

void wlr_surface_get_matrix(struct wlr_surface *surface,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	surface->impl->get_matrix(surface->state, matrix, projection, x, y);
}
