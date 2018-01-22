#include <stdlib.h>
#include <stdbool.h>
#include <wlr/render/interface.h>

void wlr_renderer_init(struct wlr_renderer *renderer,
		struct wlr_renderer_impl *impl) {
	renderer->impl = impl;
}

void wlr_renderer_destroy(struct wlr_renderer *r) {
	if (r && r->impl && r->impl->destroy) {
		r->impl->destroy(r);
	} else {
		free(r);
	}
}

void wlr_renderer_begin(struct wlr_renderer *r, struct wlr_output *o) {
	r->impl->begin(r, o);
}

void wlr_renderer_end(struct wlr_renderer *r) {
	r->impl->end(r);
}

void wlr_renderer_clear(struct wlr_renderer *r, float red, float green,
		float blue, float alpha) {
	r->impl->clear(r, red, green, blue, alpha);
}

void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box) {
	r->impl->scissor(r, box);
}

struct wlr_texture *wlr_render_texture_create(struct wlr_renderer *r) {
	return r->impl->texture_create(r);
}

bool wlr_render_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const float (*matrix)[16]) {
	return r->impl->render_with_matrix(r, texture, matrix);
}

void wlr_render_colored_quad(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]) {
	r->impl->render_quad(r, color, matrix);
}

void wlr_render_colored_ellipse(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]) {
	r->impl->render_ellipse(r, color, matrix);
}

const enum wl_shm_format *wlr_renderer_get_formats(
		struct wlr_renderer *r, size_t *len) {
	return r->impl->formats(r, len);
}

bool wlr_renderer_buffer_is_drm(struct wlr_renderer *r,
		struct wl_resource *buffer) {
	return r->impl->buffer_is_drm(r, buffer);
}

void wlr_renderer_read_pixels(struct wlr_renderer *r, int x, int y,
		int width, int height, void *out_data) {
	r->impl->read_pixels(r, x, y, width, height, out_data);
}
