#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "util/signal.h"

void wlr_renderer_init(struct wlr_renderer *renderer,
		const struct wlr_renderer_impl *impl) {
	assert(impl->begin);
	assert(impl->clear);
	assert(impl->scissor);
	assert(impl->render_subtexture_with_matrix);
	assert(impl->render_quad_with_matrix);
	assert(impl->render_ellipse_with_matrix);
	assert(impl->formats);
	assert(impl->format_supported);
	assert(impl->texture_from_pixels);
	renderer->impl = impl;

	wl_signal_init(&renderer->events.destroy);
}

void wlr_renderer_destroy(struct wlr_renderer *r) {
	if (!r) {
		return;
	}
	wlr_signal_emit_safe(&r->events.destroy, r);

	if (r->impl && r->impl->destroy) {
		r->impl->destroy(r);
	} else {
		free(r);
	}
}

void wlr_renderer_begin(struct wlr_renderer *r, int width, int height) {
	assert(!r->rendering);

	r->impl->begin(r, width, height);

	r->rendering = true;
}

void wlr_renderer_end(struct wlr_renderer *r) {
	assert(r->rendering);

	if (r->impl->end) {
		r->impl->end(r);
	}

	r->rendering = false;
}

void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]) {
	assert(r->rendering);
	r->impl->clear(r, color);
}

void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box) {
	assert(r->rendering);
	r->impl->scissor(r, box);
}

bool wlr_render_texture(struct wlr_renderer *r, struct wlr_texture *texture,
		const float projection[static 9], int x, int y, float alpha) {
	struct wlr_box box = { .x = x, .y = y };
	wlr_texture_get_size(texture, &box.width, &box.height);

	float matrix[9];
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	return wlr_render_texture_with_matrix(r, texture, matrix, alpha);
}

bool wlr_render_texture_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const float matrix[static 9],
		float alpha) {
	struct wlr_fbox box = {
		.x = 0,
		.y = 0,
		.width = texture->width,
		.height = texture->height,
	};
	return wlr_render_subtexture_with_matrix(r, texture, &box, matrix, alpha);
}

bool wlr_render_subtexture_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const struct wlr_fbox *box,
		const float matrix[static 9], float alpha) {
	assert(r->rendering);
	return r->impl->render_subtexture_with_matrix(r, texture,
		box, matrix, alpha);
}

void wlr_render_rect(struct wlr_renderer *r, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	if (box->width == 0 || box->height == 0) {
		return;
	}
	assert(box->width > 0 && box->height > 0);
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	wlr_render_quad_with_matrix(r, color, matrix);
}

void wlr_render_quad_with_matrix(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	assert(r->rendering);
	r->impl->render_quad_with_matrix(r, color, matrix);
}

void wlr_render_ellipse(struct wlr_renderer *r, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	if (box->width == 0 || box->height == 0) {
		return;
	}
	assert(box->width > 0 && box->height > 0);
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	wlr_render_ellipse_with_matrix(r, color, matrix);
}

void wlr_render_ellipse_with_matrix(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	assert(r->rendering);
	r->impl->render_ellipse_with_matrix(r, color, matrix);
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

const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_formats(
		struct wlr_renderer *r) {
	if (!r->impl->get_dmabuf_formats) {
		return NULL;
	}
	return r->impl->get_dmabuf_formats(r);
}

bool wlr_renderer_read_pixels(struct wlr_renderer *r, enum wl_shm_format fmt,
		uint32_t *flags, uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		void *data) {
	if (!r->impl->read_pixels) {
		return false;
	}
	return r->impl->read_pixels(r, fmt, flags, stride, width, height,
		src_x, src_y, dst_x, dst_y, data);
}

bool wlr_renderer_blit_dmabuf(struct wlr_renderer *r,
		struct wlr_dmabuf_attributes *dst,
		struct wlr_dmabuf_attributes *src) {
	assert(!r->rendering);
	if (!r->impl->blit_dmabuf) {
		return false;
	}
	return r->impl->blit_dmabuf(r, dst, src);
}

bool wlr_renderer_format_supported(struct wlr_renderer *r,
		enum wl_shm_format fmt) {
	return r->impl->format_supported(r, fmt);
}

bool wlr_renderer_init_wl_display(struct wlr_renderer *r,
		struct wl_display *wl_display) {
	if (wl_display_init_shm(wl_display)) {
		wlr_log(WLR_ERROR, "Failed to initialize shm");
		return false;
	}

	size_t len;
	const enum wl_shm_format *formats = wlr_renderer_get_formats(r, &len);
	if (formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to initialize shm: cannot get formats");
		return false;
	}

	for (size_t i = 0; i < len; ++i) {
		// These formats are already added by default
		if (formats[i] != WL_SHM_FORMAT_ARGB8888 &&
				formats[i] != WL_SHM_FORMAT_XRGB8888) {
			wl_display_add_shm_format(wl_display, formats[i]);
		}
	}

	if (r->impl->init_wl_display) {
		if (!r->impl->init_wl_display(r, wl_display)) {
			return false;
		}
	}

	return true;
}

struct wlr_renderer *wlr_renderer_autocreate(struct wlr_egl *egl,
		EGLenum platform, void *remote_display, EGLint *config_attribs,
		EGLint visual_id) {
	// Append GLES2-specific bits to the provided EGL config attributes
	EGLint gles2_config_attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};

	size_t config_attribs_len = 0; // not including terminating EGL_NONE
	while (config_attribs != NULL &&
			config_attribs[config_attribs_len] != EGL_NONE) {
		++config_attribs_len;
	}

	size_t all_config_attribs_len = config_attribs_len +
		sizeof(gles2_config_attribs) / sizeof(gles2_config_attribs[0]);
	EGLint all_config_attribs[all_config_attribs_len];
	if (config_attribs_len > 0) {
		memcpy(all_config_attribs, config_attribs,
			config_attribs_len * sizeof(EGLint));
	}
	memcpy(&all_config_attribs[config_attribs_len], gles2_config_attribs,
		sizeof(gles2_config_attribs));

	if (!wlr_egl_init(egl, platform, remote_display, all_config_attribs,
			visual_id)) {
		wlr_log(WLR_ERROR, "Could not initialize EGL");
		return NULL;
	}

	struct wlr_renderer *renderer = wlr_gles2_renderer_create(egl);
	if (!renderer) {
		wlr_egl_finish(egl);
	}

	return renderer;
}
