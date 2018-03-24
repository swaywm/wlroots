#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>

void wlr_renderer_init(struct wlr_renderer *renderer,
		const struct wlr_renderer_impl *impl) {
	assert(impl->begin);
	assert(impl->clear);
	assert(impl->scissor);
	assert(impl->render_texture_with_matrix);
	assert(impl->render_quad);
	assert(impl->render_ellipse);
	assert(impl->formats);
	assert(impl->format_supported);
	assert(impl->texture_from_pixels);
	renderer->impl = impl;
}

void wlr_renderer_destroy(struct wlr_renderer *r) {
	if (r && r->impl && r->impl->destroy) {
		r->impl->destroy(r);
	} else {
		free(r);
	}
}

void wlr_renderer_begin(struct wlr_renderer *r, int width, int height) {
	r->impl->begin(r, width, height);
}

void wlr_renderer_end(struct wlr_renderer *r) {
	if (r->impl->end) {
		r->impl->end(r);
	}
}

void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]) {
	r->impl->clear(r, color);
}

void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box) {
	r->impl->scissor(r, box);
}

bool wlr_render_texture(struct wlr_renderer *r, struct wlr_texture *texture,
		const float projection[static 9], int x, int y, float alpha) {
	int width, height;
	wlr_texture_get_size(texture, &width, &height);

	float mat[9];
	wlr_matrix_identity(mat);
	wlr_matrix_translate(mat, x, y);
	wlr_matrix_scale(mat, width, height);
	wlr_matrix_multiply(mat, projection, mat);

	return wlr_render_texture_with_matrix(r, texture, mat, alpha);
}

bool wlr_render_texture_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const float matrix[static 9],
		float alpha) {
	return r->impl->render_texture_with_matrix(r, texture, matrix, alpha);
}

void wlr_render_colored_quad(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	r->impl->render_quad(r, color, matrix);
}

void wlr_render_colored_ellipse(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	r->impl->render_ellipse(r, color, matrix);
}

const enum wl_shm_format *wlr_renderer_get_formats(
		struct wlr_renderer *r, size_t *len) {
	return r->impl->formats(r, len);
}

bool wlr_renderer_resource_is_wl_drm_buffer(struct wlr_renderer *r,
		struct wl_resource *resource) {
	if (!r->impl->resource_is_wl_drm_buffer) {
		return false;
	}
	return r->impl->resource_is_wl_drm_buffer(r, resource);
}

void wlr_renderer_wl_drm_buffer_get_size(struct wlr_renderer *r,
		struct wl_resource *buffer, int *width, int *height) {
	if (!r->impl->wl_drm_buffer_get_size) {
		return;
	}
	return r->impl->wl_drm_buffer_get_size(r, buffer, width, height);
}

bool wlr_renderer_read_pixels(struct wlr_renderer *r, enum wl_shm_format fmt,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		void *data) {
	if (!r->impl->read_pixels) {
		return false;
	}
	return r->impl->read_pixels(r, fmt, stride, width, height, src_x, src_y,
		dst_x, dst_y, data);
}

bool wlr_renderer_format_supported(struct wlr_renderer *r,
		enum wl_shm_format fmt) {
	return r->impl->format_supported(r, fmt);
}
