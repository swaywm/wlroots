#include <assert.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>

static bool wl_shm_buf_is_instance(struct wl_resource *resource) {
	return wl_shm_buffer_get(resource) != NULL;
}

static bool wl_shm_buf_initialize(struct wlr_buffer *buffer,
			struct wl_resource *resource, struct wlr_renderer *renderer) {
	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	assert(shm_buf);

	enum wl_shm_format fmt = wl_shm_buffer_get_format(shm_buf);
	int32_t stride = wl_shm_buffer_get_stride(shm_buf);
	int32_t width = wl_shm_buffer_get_width(shm_buf);
	int32_t height = wl_shm_buffer_get_height(shm_buf);

	wl_shm_buffer_begin_access(shm_buf);
	void *data = wl_shm_buffer_get_data(shm_buf);
	buffer->texture = wlr_texture_from_pixels(renderer, fmt, stride,
		width, height, data);
	wl_shm_buffer_end_access(shm_buf);

	// We have uploaded the data, we don't need to access the wl_buffer
	// anymore
	wl_buffer_send_release(resource);
	buffer->released = true;

	return true;
}

static bool wl_shm_buf_get_resource_size(struct wl_resource *resource,
		struct wlr_renderer *renderer, int *width, int *height) {
	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	assert(shm_buf);
	*width = wl_shm_buffer_get_width(shm_buf);
	*height = wl_shm_buffer_get_height(shm_buf);
	return true;
}

static bool wl_shm_buf_apply_damage(struct wlr_buffer *buffer,
		struct wl_resource *resource, pixman_region32_t *damage) {
	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	struct wl_shm_buffer *old_shm_buf = wl_shm_buffer_get(buffer->resource);
	if (!shm_buf || !old_shm_buf) {
		return false;
	}

	enum wl_shm_format new_fmt = wl_shm_buffer_get_format(shm_buf);
	enum wl_shm_format old_fmt = wl_shm_buffer_get_format(old_shm_buf);
	if (new_fmt != old_fmt) {
		// Uploading to textures can't change the format
		return false;
	}

	int32_t stride = wl_shm_buffer_get_stride(shm_buf);
	int32_t width = wl_shm_buffer_get_width(shm_buf);
	int32_t height = wl_shm_buffer_get_height(shm_buf);

	int32_t texture_width, texture_height;
	wlr_texture_get_size(buffer->texture, &texture_width, &texture_height);
	if (width != texture_width || height != texture_height) {
		return false;
	}

	wl_shm_buffer_begin_access(shm_buf);
	void *data = wl_shm_buffer_get_data(shm_buf);

	int n;
	pixman_box32_t *rects = pixman_region32_rectangles(damage, &n);
	for (int i = 0; i < n; ++i) {
		pixman_box32_t *r = &rects[i];
		if (!wlr_texture_write_pixels(buffer->texture, stride,
				r->x2 - r->x1, r->y2 - r->y1, r->x1, r->y1,
				r->x1, r->y1, data)) {
			wl_shm_buffer_end_access(shm_buf);
			return false;
		}
	}

	wl_shm_buffer_end_access(shm_buf);

	// We have uploaded the data, we don't need to access the wl_buffer
	// anymore
	wl_buffer_send_release(resource);
	buffer->released = true;

	return true;
}

const struct wlr_buffer_impl wl_shm_buf_implementation = {
	.is_instance = wl_shm_buf_is_instance,
	.initialize = wl_shm_buf_initialize,
	.get_resource_size = wl_shm_buf_get_resource_size,
	.apply_damage = wl_shm_buf_apply_damage,
};
