#include <stdlib.h>
#include <stdbool.h>
#include <wlr/render/interface.h>

struct wlr_renderer *wlr_renderer_init(struct wlr_renderer_state *state,
		struct wlr_renderer_impl *impl) {
	struct wlr_renderer *r = calloc(sizeof(struct wlr_renderer), 1);
	r->state = state;
	r->impl = impl;
	return r;
}

void wlr_renderer_destroy(struct wlr_renderer *r) {
	r->impl->destroy(r->state);
	free(r);
}

void wlr_renderer_begin(struct wlr_renderer *r, struct wlr_output *o) {
	r->impl->begin(r->state, o);
}

void wlr_renderer_end(struct wlr_renderer *r) {
	r->impl->end(r->state);
}

struct wlr_surface *wlr_render_surface_init(struct wlr_renderer *r) {
	return r->impl->surface_init(r->state);
}

bool wlr_render_with_matrix(struct wlr_renderer *r,
		struct wlr_surface *surface, const float (*matrix)[16]) {
	return r->impl->render_with_matrix(r->state, surface, matrix);
}

void wlr_render_colored_quad(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]) {
	r->impl->render_quad(r->state, color, matrix);
}

void wlr_render_colored_ellipse(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]) {
	r->impl->render_ellipse(r->state, color, matrix);
}

const enum wl_shm_format *wlr_renderer_get_formats(
		struct wlr_renderer *r, size_t *len) {
	return r->impl->formats(r->state, len);
}
