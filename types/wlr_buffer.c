#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_linux_dmabuf.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>

bool wlr_resource_is_buffer(struct wl_resource *resource) {
	return strcmp(wl_resource_get_class(resource), wl_buffer_interface.name) == 0;
}

bool wlr_buffer_get_resource_size(struct wl_resource *resource,
		struct wlr_renderer *renderer, int *width, int *height) {
	assert(wlr_resource_is_buffer(resource));

	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	if (shm_buf != NULL) {
		*width = wl_shm_buffer_get_width(shm_buf);
		*height = wl_shm_buffer_get_height(shm_buf);
	} else if (wlr_renderer_resource_is_wl_drm_buffer(renderer,
			resource)) {
		wlr_renderer_wl_drm_buffer_get_size(renderer, resource,
			width, height);
	} else if (wlr_dmabuf_resource_is_buffer(resource)) {
		struct wlr_dmabuf_buffer *dmabuf =
			wlr_dmabuf_buffer_from_buffer_resource(resource);
		*width = dmabuf->attributes.width;
		*height = dmabuf->attributes.height;
	} else {
		*width = *height = 0;
		return false;
	}

	return true;
}


static void buffer_resource_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_buffer *buffer =
		wl_container_of(listener, buffer, resource_destroy);
	wl_list_remove(&buffer->resource_destroy.link);
	wl_list_init(&buffer->resource_destroy.link);
	buffer->resource = NULL;

	if (!buffer->released) {
		// The texture becomes invalid
		wlr_texture_destroy(buffer->texture);
		buffer->texture = NULL;
	}
}

struct wlr_buffer *wlr_buffer_create(struct wlr_renderer *renderer,
		struct wl_resource *resource) {
	assert(wlr_resource_is_buffer(resource));

	struct wlr_texture *texture = NULL;
	bool released = false;

	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	if (shm_buf != NULL) {
		enum wl_shm_format fmt = wl_shm_buffer_get_format(shm_buf);
		int32_t stride = wl_shm_buffer_get_stride(shm_buf);
		int32_t width = wl_shm_buffer_get_width(shm_buf);
		int32_t height = wl_shm_buffer_get_height(shm_buf);

		wl_shm_buffer_begin_access(shm_buf);
		void *data = wl_shm_buffer_get_data(shm_buf);
		texture = wlr_texture_from_pixels(renderer, fmt, stride,
			width, height, data);
		wl_shm_buffer_end_access(shm_buf);

		// We have uploaded the data, we don't need to access the wl_buffer
		// anymore
		wl_buffer_send_release(resource);
		released = true;
	} else if (wlr_renderer_resource_is_wl_drm_buffer(renderer, resource)) {
		texture = wlr_texture_from_wl_drm(renderer, resource);
	} else if (wlr_dmabuf_resource_is_buffer(resource)) {
		struct wlr_dmabuf_buffer *dmabuf =
			wlr_dmabuf_buffer_from_buffer_resource(resource);
		texture = wlr_texture_from_dmabuf(renderer, &dmabuf->attributes);
	} else {
		wlr_log(L_ERROR, "Cannot upload texture: unknown buffer type");
		return NULL;
	}

	if (texture == NULL) {
		wlr_log(L_ERROR, "Failed to upload texture");
		return NULL;
	}

	struct wlr_buffer *buffer = calloc(1, sizeof(struct wlr_buffer));
	if (buffer == NULL) {
		return NULL;
	}
	buffer->resource = resource;
	buffer->texture = texture;
	buffer->released = released;
	buffer->n_refs = 1;

	wl_resource_add_destroy_listener(resource, &buffer->resource_destroy);
	buffer->resource_destroy.notify = buffer_resource_handle_destroy;

	return buffer;
}

struct wlr_buffer *wlr_buffer_ref(struct wlr_buffer *buffer) {
	buffer->n_refs++;
	return buffer;
}

void wlr_buffer_unref(struct wlr_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}

	assert(buffer->n_refs > 0);
	buffer->n_refs--;
	if (buffer->n_refs > 0) {
		return;
	}

	if (!buffer->released && buffer->resource != NULL) {
		wl_buffer_send_release(buffer->resource);
	}

	wl_list_remove(&buffer->resource_destroy.link);
	wlr_texture_destroy(buffer->texture);
	free(buffer);
}

struct wlr_buffer *wlr_buffer_apply_damage(struct wlr_buffer *buffer,
		struct wl_resource *resource, pixman_region32_t *damage) {
	assert(wlr_resource_is_buffer(resource));

	if (buffer->n_refs > 1) {
		// Someone else still has a reference to the buffer
		return NULL;
	}

	struct wl_shm_buffer *shm_buf = wl_shm_buffer_get(resource);
	if (shm_buf == NULL) {
		// Uploading only damaged regions only works for wl_shm buffers
		return NULL;
	}

	enum wl_shm_format fmt = wl_shm_buffer_get_format(shm_buf);
	int32_t stride = wl_shm_buffer_get_stride(shm_buf);
	int32_t width = wl_shm_buffer_get_width(shm_buf);
	int32_t height = wl_shm_buffer_get_height(shm_buf);

	int32_t texture_width, texture_height;
	wlr_texture_get_size(buffer->texture, &texture_width, &texture_height);
	if (width != texture_width || height != texture_height) {
		return NULL;
	}

	wl_shm_buffer_begin_access(shm_buf);
	void *data = wl_shm_buffer_get_data(shm_buf);

	int n;
	pixman_box32_t *rects = pixman_region32_rectangles(damage, &n);
	for (int i = 0; i < n; ++i) {
		pixman_box32_t *r = &rects[i];
		if (!wlr_texture_write_pixels(buffer->texture, fmt, stride,
				r->x2 - r->x1, r->y2 - r->y1, r->x1, r->y1,
				r->x1, r->y1, data)) {
			wl_shm_buffer_end_access(shm_buf);
			return NULL;
		}
	}

	wl_shm_buffer_end_access(shm_buf);

	// We have uploaded the data, we don't need to access the wl_buffer
	// anymore
	wl_buffer_send_release(resource);

	wl_list_remove(&buffer->resource_destroy.link);
	wl_resource_add_destroy_listener(resource, &buffer->resource_destroy);
	buffer->resource_destroy.notify = buffer_resource_handle_destroy;

	buffer->resource = resource;
	buffer->released = true;
	return buffer;
}
