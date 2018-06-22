#include <assert.h>
#include <stdlib.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/backend.h>
#include "wlr-screencopy-unstable-v1-protocol.h"

#define SCREENCOPY_MANAGER_VERSION 1

static const struct zwlr_screencopy_frame_v1_interface frame_impl;

static struct wlr_screencopy_frame_v1 *frame_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_screencopy_frame_v1_interface, &frame_impl));
	return wl_resource_get_user_data(resource);
}

static void frame_handle_output_swap_buffers(struct wl_listener *listener,
		void *_data) {
	struct wlr_screencopy_frame_v1 *frame =
		wl_container_of(listener, frame, output_swap_buffers);
	struct wlr_output_event_swap_buffers *event = _data;
	struct wlr_output *output = frame->output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);

	wl_list_remove(&frame->output_swap_buffers.link);
	wl_list_init(&frame->output_swap_buffers.link);

	if (output->width != frame->width || output->height != frame->height) {
		zwlr_screencopy_frame_v1_send_failed(frame->resource);
		return;
	}

	struct wl_shm_buffer *buffer = frame->buffer;
	assert(buffer != NULL);

	enum wl_shm_format fmt = wl_shm_buffer_get_format(buffer);
	int32_t width = wl_shm_buffer_get_width(buffer);
	int32_t height = wl_shm_buffer_get_height(buffer);
	int32_t stride = wl_shm_buffer_get_stride(buffer);

	wl_shm_buffer_begin_access(buffer);
	void *data = wl_shm_buffer_get_data(buffer);
	bool ok = wlr_renderer_read_pixels(renderer, fmt, stride, width, height,
		0, 0, 0, 0, data);
	wl_shm_buffer_end_access(buffer);

	if (!ok) {
		zwlr_screencopy_frame_v1_send_failed(frame->resource);
		return;
	}

	uint32_t tv_sec_hi = event->when->tv_sec >> 32;
	uint32_t tv_sec_lo = event->when->tv_sec & 0xFFFFFFFF;
	zwlr_screencopy_frame_v1_send_ready(frame->resource,
		tv_sec_hi, tv_sec_lo, event->when->tv_nsec);

	// TODO: make frame resource inert
}

static void frame_handle_copy(struct wl_client *client,
		struct wl_resource *frame_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_screencopy_frame_v1 *frame = frame_from_resource(frame_resource);
	struct wlr_output *output = frame->output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);

	struct wl_shm_buffer *buffer = wl_shm_buffer_get(buffer_resource);
	if (buffer == NULL) {
		zwlr_screencopy_frame_v1_send_failed(frame_resource);
		return;
	}

	enum wl_shm_format fmt = wl_shm_buffer_get_format(buffer);
	if (!wlr_renderer_format_supported(renderer, fmt)) {
		wl_resource_post_error(frame->resource,
			ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_FORMAT,
			"unsupported format %"PRIu32, fmt);
		return;
	}

	if (frame->width != wl_shm_buffer_get_width(buffer) ||
			frame->height != wl_shm_buffer_get_height(buffer)) {
		wl_resource_post_error(frame->resource,
			ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_DIMENSIONS,
			"invalid width or height");
		return;
	}

	if (!wl_list_empty(&frame->output_swap_buffers.link) ||
			frame->buffer != NULL) {
		wl_resource_post_error(frame->resource,
			ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED,
			"frame already used");
		return;
	}

	frame->buffer = buffer;

	wl_signal_add(&output->events.swap_buffers, &frame->output_swap_buffers);
	frame->output_swap_buffers.notify = frame_handle_output_swap_buffers;

	// Schedule a buffer swap
	output->needs_swap = true;
	wlr_output_schedule_frame(output);

	// TODO: listen to buffer destroy
}

static void frame_handle_destroy(struct wl_client *client,
		struct wl_resource *frame_resource) {
	wl_resource_destroy(frame_resource);
}

static const struct zwlr_screencopy_frame_v1_interface frame_impl = {
	.copy = frame_handle_copy,
	.destroy = frame_handle_destroy,
};

static void frame_handle_resource_destroy(struct wl_resource *frame_resource) {
	struct wlr_screencopy_frame_v1 *frame = frame_from_resource(frame_resource);
	wl_list_remove(&frame->link);
	wl_list_remove(&frame->output_swap_buffers.link);
	free(frame);
}


static const struct zwlr_screencopy_manager_v1_interface manager_impl;

static struct wlr_screencopy_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_screencopy_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void manager_handle_capture_output(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		int32_t overlay_cursor, struct wl_resource *output_resource) {
	struct wlr_screencopy_manager_v1 *manager =
		manager_from_resource(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	struct wlr_screencopy_frame_v1 *frame =
		calloc(1, sizeof(struct wlr_screencopy_frame_v1));
	if (frame == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	frame->manager = manager;

	frame->output = output;

	uint32_t version = wl_resource_get_version(manager_resource);
	frame->resource = wl_resource_create(client,
		&zwlr_screencopy_frame_v1_interface, version, id);
	if (frame->resource == NULL) {
		free(frame);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(frame->resource, &frame_impl, frame,
		frame_handle_resource_destroy);

	wl_list_insert(&manager->frames, &frame->link);

	wl_list_init(&frame->output_swap_buffers.link);

	frame->width = output->width;
	frame->height = output->height;
	// TODO: don't send zero
	zwlr_screencopy_frame_v1_send_buffer(frame->resource,
		frame->width, frame->height, 0, 0, 0);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwlr_screencopy_manager_v1_interface manager_impl = {
	.capture_output = manager_handle_capture_output,
	.destroy = manager_handle_destroy,
};

void manager_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	struct wlr_screencopy_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_screencopy_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager,
		manager_handle_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_screencopy_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_screencopy_manager_v1_destroy(manager);
}

struct wlr_screencopy_manager_v1 *wlr_screencopy_manager_v1_create(
		struct wl_display *display) {
	struct wlr_screencopy_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_screencopy_manager_v1));
	if (manager == NULL) {
		return NULL;
	}

	manager->global = wl_global_create(display,
		&zwlr_screencopy_manager_v1_interface, SCREENCOPY_MANAGER_VERSION,
		manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}
	wl_list_init(&manager->resources);
	wl_list_init(&manager->frames);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

void wlr_screencopy_manager_v1_destroy(
		struct wlr_screencopy_manager_v1 *manager) {
	if (manager == NULL) {
		return;
	}
	wl_list_remove(&manager->display_destroy.link);
	struct wlr_screencopy_frame_v1 *frame, *tmp_frame;
	wl_list_for_each_safe(frame, tmp_frame, &manager->frames, link) {
		wl_resource_destroy(frame->resource);
	}
	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &manager->resources) {
		wl_resource_destroy(resource);
	}
	wl_global_destroy(manager->global);
	free(manager);
}
