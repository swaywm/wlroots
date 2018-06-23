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

static void frame_destroy(struct wlr_screencopy_frame_v1 *frame) {
	if (frame == NULL) {
		return;
	}
	wl_list_remove(&frame->link);
	wl_list_remove(&frame->output_swap_buffers.link);
	// Make the frame resource inert
	wl_resource_set_user_data(frame->resource, NULL);
	free(frame);
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

	int x = frame->box.x;
	int y = frame->box.y;

	struct wl_shm_buffer *buffer = frame->buffer;
	assert(buffer != NULL);

	enum wl_shm_format fmt = wl_shm_buffer_get_format(buffer);
	int32_t width = wl_shm_buffer_get_width(buffer);
	int32_t height = wl_shm_buffer_get_height(buffer);
	int32_t stride = wl_shm_buffer_get_stride(buffer);

	wl_shm_buffer_begin_access(buffer);
	void *data = wl_shm_buffer_get_data(buffer);
	uint32_t flags = 0;
	bool ok = wlr_renderer_read_pixels(renderer, fmt, &flags, stride,
		width, height, x, y, 0, 0, data);
	wl_shm_buffer_end_access(buffer);

	if (!ok) {
		zwlr_screencopy_frame_v1_send_failed(frame->resource);
		frame_destroy(frame);
		return;
	}

	zwlr_screencopy_frame_v1_send_flags(frame->resource, flags);

	uint32_t tv_sec_hi = event->when->tv_sec >> 32;
	uint32_t tv_sec_lo = event->when->tv_sec & 0xFFFFFFFF;
	zwlr_screencopy_frame_v1_send_ready(frame->resource,
		tv_sec_hi, tv_sec_lo, event->when->tv_nsec);

	frame_destroy(frame);
}

static void frame_handle_copy(struct wl_client *client,
		struct wl_resource *frame_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_screencopy_frame_v1 *frame = frame_from_resource(frame_resource);
	if (frame == NULL) {
		return;
	}

	struct wlr_output *output = frame->output;

	struct wl_shm_buffer *buffer = wl_shm_buffer_get(buffer_resource);
	if (buffer == NULL) {
		wl_resource_post_error(frame->resource,
			ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
			"unsupported buffer type");
		return;
	}

	enum wl_shm_format fmt = wl_shm_buffer_get_format(buffer);
	int32_t width = wl_shm_buffer_get_width(buffer);
	int32_t height = wl_shm_buffer_get_height(buffer);
	int32_t stride = wl_shm_buffer_get_stride(buffer);
	if (fmt != frame->format || width != frame->box.width ||
			height != frame->box.height || stride != frame->stride) {
		wl_resource_post_error(frame->resource,
			ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER,
			"invalid buffer attributes");
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
	frame_destroy(frame);
}


static const struct zwlr_screencopy_manager_v1_interface manager_impl;

static struct wlr_screencopy_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_screencopy_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void capture_output(struct wl_client *client,
		struct wlr_screencopy_manager_v1 *manager, uint32_t version, uint32_t id,
		int32_t overlay_cursor, struct wlr_output *output,
		const struct wlr_box *box) {
	struct wlr_box buffer_box = {0};
	if (box == NULL) {
		buffer_box.width = output->width;
		buffer_box.height = output->height;
	} else {
		int ow, oh;
		wlr_output_effective_resolution(output, &ow, &oh);

		buffer_box = *box;

		wlr_box_transform(&buffer_box, output->transform, ow, oh, &buffer_box);
		buffer_box.x *= output->scale;
		buffer_box.y *= output->scale;
		buffer_box.width *= output->scale;
		buffer_box.height *= output->scale;
	}

	struct wlr_screencopy_frame_v1 *frame =
		calloc(1, sizeof(struct wlr_screencopy_frame_v1));
	if (frame == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	frame->manager = manager;
	frame->output = output;

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

	frame->format = WL_SHM_FORMAT_XRGB8888;
	frame->box = buffer_box;
	frame->stride = 4 * buffer_box.width;
	zwlr_screencopy_frame_v1_send_buffer(frame->resource, frame->format,
		buffer_box.width, buffer_box.height, frame->stride);
}

static void manager_handle_capture_output(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		int32_t overlay_cursor, struct wl_resource *output_resource) {
	struct wlr_screencopy_manager_v1 *manager =
		manager_from_resource(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	capture_output(client, manager, version, id, overlay_cursor, output, NULL);
}

static void manager_handle_capture_output_region(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		int32_t overlay_cursor, struct wl_resource *output_resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_screencopy_manager_v1 *manager =
		manager_from_resource(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	struct wlr_box box = {
		.x = x,
		.y = y,
		.width = width,
		.height = height,
	};
	capture_output(client, manager, version, id, overlay_cursor, output, &box);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwlr_screencopy_manager_v1_interface manager_impl = {
	.capture_output = manager_handle_capture_output,
	.capture_output_region = manager_handle_capture_output_region,
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
