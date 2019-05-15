#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pixman.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "util/signal.h"

void wlr_renderer_init(struct wlr_renderer *renderer,
		const struct wlr_renderer_impl *impl) {
	assert(impl->begin);
	assert(impl->clear);
	assert(impl->scissor);
	assert(impl->render_texture_with_matrix);
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
	return r->impl->render_texture_with_matrix(r, texture, matrix, alpha);
}

void wlr_render_rect(struct wlr_renderer *r, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	wlr_render_quad_with_matrix(r, color, matrix);
}

void wlr_render_quad_with_matrix(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	r->impl->render_quad_with_matrix(r, color, matrix);
}

void wlr_render_ellipse(struct wlr_renderer *r, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	wlr_render_ellipse_with_matrix(r, color, matrix);
}

void wlr_render_ellipse_with_matrix(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
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

bool wlr_renderer_format_supported(struct wlr_renderer *r,
		enum wl_shm_format fmt) {
	return r->impl->format_supported(r, fmt);
}

struct buffer {
	struct wlr_buffer base;
	struct wl_list link;

	bool released;
	struct wlr_texture *texture;

	struct wl_listener resource_destroy;
};

static void buffer_resource_destroy(struct wl_listener *listener, void *data) {
	struct buffer *buf = wl_container_of(listener, buf, resource_destroy);

	buf->base.resource = NULL;

	/*
	 * At this point, if the wl_buffer comes from a dmabuf-based buffer, we
	 * still haven't released it (i.e. we'll read it in the future) but the
	 * client destroyed it. Reading the texture itself should be fine
	 * because we still hold a reference to the dmabuf via the texture.
	 * However the client could decide to re-use the same dmabuf for
	 * something else, in which case we'll read garbage. We decide to
	 * accept this risk.
	 */
}

static struct wlr_buffer *create_buffer(struct wlr_renderer *renderer,
		struct wl_resource *res) {
	struct buffer *buf = calloc(1, sizeof(*buf));
	if (!buf) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		wl_resource_post_no_memory(res);
		return NULL;
	}

	struct wl_shm_buffer *shm = wl_shm_buffer_get(res);
	if (shm) {
		enum wl_shm_format fmt = wl_shm_buffer_get_format(shm);
		int32_t stride = wl_shm_buffer_get_stride(shm);
		int32_t width = wl_shm_buffer_get_width(shm);
		int32_t height = wl_shm_buffer_get_height(shm);

		wl_shm_buffer_begin_access(shm);
		void *data = wl_shm_buffer_get_data(shm);
		buf->texture = wlr_texture_from_pixels(renderer, fmt, stride,
			width, height, data);
		wl_shm_buffer_end_access(shm);

		/*
		 * We have uploaded the data, we don't need to access the
		 * wl_buffer anymore
		 */
		wl_buffer_send_release(res);
		buf->released = true;
		buf->base.protocol = "wl_shm";
	} else if (wlr_renderer_resource_is_wl_drm_buffer(renderer, res)) {
		buf->texture = wlr_texture_from_wl_drm(renderer, res);
		buf->base.protocol = "egl";
	} else if (wlr_dmabuf_v1_resource_is_buffer(res)) {
		struct wlr_dmabuf_v1_buffer *dmabuf =
			wlr_dmabuf_v1_buffer_from_buffer_resource(res);
		buf->texture = wlr_texture_from_dmabuf(renderer, &dmabuf->attributes);
		buf->base.protocol = "zwp_linux_dmabuf_v1";
	} else {
		wlr_log(WLR_ERROR, "Cannot upload texture: unknown buffer type");

		// Instead of just logging the error, also disconnect the client with a
		// fatal protocol error so that it's clear something went wrong.
		wl_resource_post_error(res, 0, "unknown buffer type");
		free(buf);
		return NULL;
	}

	if (!buf->texture) {
		wlr_log(WLR_ERROR, "Failed to create texture");
		wl_resource_post_error(res, 0, "failed to use buffer");
		free(buf);
		return NULL;
	}

	int width, height;
	wlr_texture_get_size(buf->texture, &width, &height);
	buf->base.width = width;
	buf->base.height = height;
	buf->base.ref_cnt = 1;

	return &buf->base;
}

static struct wlr_buffer *buffer_ref(struct wlr_buffer *buffer) {
	assert(buffer);
	++buffer->ref_cnt;
	return buffer;
}

static void buffer_unref(struct wlr_buffer *buffer_base) {
	struct buffer *buffer = wl_container_of(buffer_base, buffer, base);
	assert(buffer->base.ref_cnt > 0);

	if (--buffer->base.ref_cnt > 0) {
		return;
	}

	if (buffer->base.resource) {
		if (!buffer->released) {
			wl_buffer_send_release(buffer->base.resource);
		}
		wl_list_remove(&buffer->resource_destroy.link);
	}

	wlr_texture_destroy(buffer->texture);
	free(buffer);
}

static struct wlr_buffer *buffer_create(void *userdata,
		struct wlr_buffer *buffer_base, pixman_region32_t *buffer_damage,
		struct wl_resource *res) {
	struct wlr_renderer *renderer = userdata;
	struct buffer *buffer = wl_container_of(buffer_base, buffer, base);

	if (!buffer) {
		return create_buffer(renderer, res);
	}

	/*
	 * The old buffer is still being used somewhere else, so we cannot
	 * change/reuse it.
	 */
	if (buffer->base.ref_cnt != 1) {
		buffer_unref(&buffer->base);
		return create_buffer(renderer, res);
	}

	/*
	 * Any kind of buffer sharing only happens with wl_shm.
	 * dmabuf-based textures are cheap to create and aren't modifiable.
	 */
	struct wl_shm_buffer *old_shm = wl_shm_buffer_get(buffer->base.resource);
	struct wl_shm_buffer *new_shm = wl_shm_buffer_get(res);
	if (!old_shm || !new_shm) {
		buffer_unref(&buffer->base);
		return create_buffer(renderer, res);
	}

	/*
	 * We can't change the shm format.
	 */
	enum wl_shm_format old_fmt = wl_shm_buffer_get_format(old_shm);
	enum wl_shm_format new_fmt = wl_shm_buffer_get_format(new_shm);
	if (old_fmt != new_fmt) {
		buffer_unref(&buffer->base);
		return create_buffer(renderer, res);
	}

	/*
	 * We can't change the dimensions.
	 */
	int32_t stride = wl_shm_buffer_get_stride(new_shm);
	int32_t width = wl_shm_buffer_get_width(new_shm);
	int32_t height = wl_shm_buffer_get_height(new_shm);
	int old_width, old_height;
	wlr_texture_get_size(buffer->texture, &old_width, &old_height);
	if (width != old_width || height != old_height) {
		buffer_unref(&buffer->base);
		return create_buffer(renderer, res);
	}

	wl_shm_buffer_begin_access(new_shm);
	void *data = wl_shm_buffer_get_data(new_shm);

	/*
	 * Apply damage to buffer.
	 */
	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, width, height);
	pixman_region32_intersect(&damage, &damage, buffer_damage);
	int n;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &n);
	for (int i = 0; i < n; ++i) {
		pixman_box32_t *r = &rects[i];
		if (!wlr_texture_write_pixels(buffer->texture, stride,
				r->x2 - r->x1, r->y2 - r->y1, r->x1, r->y1,
				r->x1, r->y1, data)) {
			wlr_log(WLR_ERROR, "Failed to write pixels");
			wl_resource_post_error(res, 0, "failed to use buffer");
			buffer_unref(&buffer->base);
			wl_shm_buffer_end_access(new_shm);
			pixman_region32_fini(&damage);
			return NULL;
		}
	}

	wl_shm_buffer_end_access(new_shm);
	pixman_region32_fini(&damage);
	/*
	 * We've copied the data into a local GPU buffer, so we don't need to
	 * hold onto the wl_buffer anymore.
	 */
	wl_buffer_send_release(res);

	wl_list_remove(&buffer->resource_destroy.link);
	wl_resource_add_destroy_listener(res, &buffer->resource_destroy);
	buffer->resource_destroy.notify = buffer_resource_destroy;

	buffer->base.resource = res;
	buffer->released = true;

	return &buffer->base;
}


static const struct wlr_compositor_buffer_impl buffer_impl = {
	.create = buffer_create,
	.ref = buffer_ref,
	.unref = buffer_unref,
};

void wlr_renderer_set_compositor(struct wlr_renderer *renderer,
		struct wlr_compositor *comp) {
	assert(renderer);
	assert(comp);

	struct wl_display *display = comp->display;

	size_t len;
	const enum wl_shm_format *formats = wlr_renderer_get_formats(renderer, &len);
	if (formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to initialize shm: cannot get formats");
		return;
	}

	for (size_t i = 0; i < len; ++i) {
		// These formats are already added by default
		if (formats[i] != WL_SHM_FORMAT_ARGB8888 &&
				formats[i] != WL_SHM_FORMAT_XRGB8888) {
			wl_display_add_shm_format(display, formats[i]);
		}
	}

	if (wl_display_init_shm(display)) {
		wlr_log(WLR_ERROR, "Failed to initialize shm");
		return;
	}

	if (renderer->impl->texture_from_dmabuf) {
		wlr_linux_dmabuf_v1_create(display, renderer);
	}

	if (renderer->impl->init_wl_display) {
		renderer->impl->init_wl_display(renderer, display);
	}

	wlr_compositor_set_buffer_impl(comp, renderer, &buffer_impl);
}

struct wlr_texture *wlr_commit_get_texture(struct wlr_commit *commit) {
	assert(commit);

	if (commit->buffer) {
		struct buffer *buffer = wl_container_of(commit->buffer, buffer, base);
		return buffer->texture;
	}

	return NULL;
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
