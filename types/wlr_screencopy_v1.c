#include <assert.h>
#include <stdlib.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/backend.h>
#include <wlr/util/log.h>
#include "wlr-screencopy-unstable-v1-protocol.h"
#include "util/signal.h"

#define SCREENCOPY_MANAGER_VERSION 2

struct screencopy_damage {
	struct wl_list link;
	struct wlr_output *output;
	struct pixman_region32 damage;
	struct wl_listener output_precommit;
	struct wl_listener output_destroy;
	uint32_t last_commit_seq;
};

static const struct zwlr_screencopy_frame_v1_interface frame_impl;

static struct screencopy_damage *screencopy_damage_find(
		struct wlr_screencopy_v1_client *client,
		struct wlr_output *output) {
	struct screencopy_damage *damage;

	wl_list_for_each(damage, &client->damages, link) {
		if (damage->output == output) {
			return damage;
		}
	}

	return NULL;
}

static void screencopy_damage_accumulate(struct screencopy_damage *damage) {
	struct pixman_region32 *region = &damage->damage;
	struct wlr_output *output = damage->output;

	/* This check is done so damage that has been added and cleared in the
	 * frame precommit handler is not added again after it has been handled.
	 */
	if (damage->last_commit_seq == output->commit_seq) {
		return;
	}

	pixman_region32_union(region, region, &output->pending.damage);
	pixman_region32_intersect_rect(region, region, 0, 0, output->width,
		output->height);
	damage->last_commit_seq = output->commit_seq;
}

static void screencopy_damage_handle_output_precommit(
		struct wl_listener *listener, void *data) {
	struct screencopy_damage *damage =
		wl_container_of(listener, damage, output_precommit);
	screencopy_damage_accumulate(damage);
}

static void screencopy_damage_destroy(struct screencopy_damage *damage) {
	wl_list_remove(&damage->output_destroy.link);
	wl_list_remove(&damage->output_precommit.link);
	wl_list_remove(&damage->link);
	pixman_region32_fini(&damage->damage);
	free(damage);
}

static void screencopy_damage_handle_output_destroy(
		struct wl_listener *listener, void *data) {
	struct screencopy_damage *damage =
		wl_container_of(listener, damage, output_destroy);
	screencopy_damage_destroy(damage);
}

static struct screencopy_damage *screencopy_damage_create(
		struct wlr_screencopy_v1_client *client,
		struct wlr_output *output) {
	struct screencopy_damage *damage =
		calloc(1, sizeof(struct screencopy_damage));
	if (!damage) {
		return NULL;
	}

	damage->output = output;
	damage->last_commit_seq = output->commit_seq - 1;
	pixman_region32_init_rect(&damage->damage, 0, 0, output->width,
		output->height);
	wl_list_insert(&client->damages, &damage->link);

	wl_signal_add(&output->events.precommit, &damage->output_precommit);
	damage->output_precommit.notify =
		screencopy_damage_handle_output_precommit;

	wl_signal_add(&output->events.destroy, &damage->output_destroy);
	damage->output_destroy.notify = screencopy_damage_handle_output_destroy;

	return damage;
}

static struct screencopy_damage *screencopy_damage_get_or_create(
		struct wlr_screencopy_v1_client *client,
		struct wlr_output *output) {
	struct screencopy_damage *damage = screencopy_damage_find(client, output);
	return damage ? damage : screencopy_damage_create(client, output);
}

static void client_unref(struct wlr_screencopy_v1_client *client) {
	assert(client->ref > 0);

	if (--client->ref != 0) {
		return;
	}

	struct screencopy_damage *damage, *tmp_damage;
	wl_list_for_each_safe(damage, tmp_damage, &client->damages, link) {
		screencopy_damage_destroy(damage);
	}

	free(client);
}

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
	if (frame->output != NULL && frame->buffer != NULL) {
		wlr_output_lock_attach_render(frame->output, false);
		if (frame->cursor_locked) {
			wlr_output_lock_software_cursors(frame->output, false);
		}
	}
	wl_list_remove(&frame->link);
	wl_list_remove(&frame->output_precommit.link);
	wl_list_remove(&frame->output_destroy.link);
	wl_list_remove(&frame->output_enable.link);
	wl_list_remove(&frame->buffer_destroy.link);
	// Make the frame resource inert
	wl_resource_set_user_data(frame->resource, NULL);
	client_unref(frame->client);
	free(frame);
}

static void frame_handle_output_precommit(struct wl_listener *listener,
		void *_data) {
	struct wlr_screencopy_frame_v1 *frame =
		wl_container_of(listener, frame, output_precommit);
	struct wlr_output_event_precommit *event = _data;
	struct wlr_output *output = frame->output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	if (!(output->pending.committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}

	struct screencopy_damage *damage = NULL;
	if (frame->with_damage) {
		damage = screencopy_damage_get_or_create(frame->client, output);
		if (damage) {
			screencopy_damage_accumulate(damage);
			if (!pixman_region32_not_empty(&damage->damage)) {
				return;
			}
		}
	}

	wl_list_remove(&frame->output_precommit.link);
	wl_list_init(&frame->output_precommit.link);

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

	// TODO: send fine-grained damage events
	if (damage) {
		struct pixman_box32 *damage_box =
			pixman_region32_extents(&damage->damage);

		int damage_x = damage_box->x1;
		int damage_y = damage_box->y1;
		int damage_width = damage_box->x2 - damage_box->x1;
		int damage_height = damage_box->y2 - damage_box->y1;

		zwlr_screencopy_frame_v1_send_damage(frame->resource,
			damage_x, damage_y, damage_width, damage_height);

		pixman_region32_clear(&damage->damage);
	}

	time_t tv_sec = event->when->tv_sec;
	uint32_t tv_sec_hi = (sizeof(tv_sec) > 4) ? tv_sec >> 32 : 0;
	uint32_t tv_sec_lo = tv_sec & 0xFFFFFFFF;
	zwlr_screencopy_frame_v1_send_ready(frame->resource,
		tv_sec_hi, tv_sec_lo, event->when->tv_nsec);

	frame_destroy(frame);
}

static void frame_handle_output_enable(struct wl_listener *listener,
		void *data) {
	struct wlr_screencopy_frame_v1 *frame =
		wl_container_of(listener, frame, output_enable);
	if (!frame->output->enabled) {
		zwlr_screencopy_frame_v1_send_failed(frame->resource);
		frame_destroy(frame);
	}
}

static void frame_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_screencopy_frame_v1 *frame =
		wl_container_of(listener, frame, output_destroy);
	zwlr_screencopy_frame_v1_send_failed(frame->resource);
	frame_destroy(frame);
}

static void frame_handle_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_screencopy_frame_v1 *frame =
		wl_container_of(listener, frame, buffer_destroy);
	zwlr_screencopy_frame_v1_send_failed(frame->resource);
	frame_destroy(frame);
}

static void frame_handle_copy(struct wl_client *wl_client,
		struct wl_resource *frame_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_screencopy_frame_v1 *frame = frame_from_resource(frame_resource);
	if (frame == NULL) {
		return;
	}

	struct wlr_output *output = frame->output;

	if (!output->enabled) {
		zwlr_screencopy_frame_v1_send_failed(frame->resource);
		frame_destroy(frame);
		return;
	}

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

	if (!wl_list_empty(&frame->output_precommit.link) ||
			frame->buffer != NULL) {
		wl_resource_post_error(frame->resource,
			ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED,
			"frame already used");
		return;
	}

	frame->buffer = buffer;

	wl_signal_add(&output->events.precommit, &frame->output_precommit);
	frame->output_precommit.notify = frame_handle_output_precommit;

	wl_signal_add(&output->events.destroy, &frame->output_enable);
	frame->output_enable.notify = frame_handle_output_enable;

	wl_signal_add(&output->events.destroy, &frame->output_destroy);
	frame->output_destroy.notify = frame_handle_output_destroy;

	wl_resource_add_destroy_listener(buffer_resource, &frame->buffer_destroy);
	frame->buffer_destroy.notify = frame_handle_buffer_destroy;

	// Schedule a buffer commit
	output->needs_frame = true;
	wlr_output_schedule_frame(output);

	wlr_output_lock_attach_render(output, true);
	if (frame->overlay_cursor) {
		wlr_output_lock_software_cursors(output, true);
		frame->cursor_locked = true;
	}
}

static void frame_handle_copy_with_damage(struct wl_client *wl_client,
		struct wl_resource *frame_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_screencopy_frame_v1 *frame = frame_from_resource(frame_resource);
	if (frame == NULL) {
		return;
	}
	frame->with_damage = true;
	frame_handle_copy(wl_client, frame_resource, buffer_resource);
}

static void frame_handle_destroy(struct wl_client *wl_client,
		struct wl_resource *frame_resource) {
	wl_resource_destroy(frame_resource);
}

static const struct zwlr_screencopy_frame_v1_interface frame_impl = {
	.copy = frame_handle_copy,
	.destroy = frame_handle_destroy,
	.copy_with_damage = frame_handle_copy_with_damage,
};

static void frame_handle_resource_destroy(struct wl_resource *frame_resource) {
	struct wlr_screencopy_frame_v1 *frame = frame_from_resource(frame_resource);
	frame_destroy(frame);
}


static const struct zwlr_screencopy_manager_v1_interface manager_impl;

static struct wlr_screencopy_v1_client *client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_screencopy_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void capture_output(struct wl_client *wl_client,
		struct wlr_screencopy_v1_client *client, uint32_t version,
		uint32_t id, int32_t overlay_cursor, struct wlr_output *output,
		const struct wlr_box *box) {
	struct wlr_screencopy_frame_v1 *frame =
		calloc(1, sizeof(struct wlr_screencopy_frame_v1));
	if (frame == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	frame->output = output;
	frame->overlay_cursor = !!overlay_cursor;

	frame->resource = wl_resource_create(wl_client,
		&zwlr_screencopy_frame_v1_interface, version, id);
	if (frame->resource == NULL) {
		free(frame);
		wl_client_post_no_memory(wl_client);
		return;
	}

	frame->client = client;
	client->ref++;

	wl_resource_set_implementation(frame->resource, &frame_impl, frame,
		frame_handle_resource_destroy);

	wl_list_insert(&client->manager->frames, &frame->link);

	wl_list_init(&frame->output_precommit.link);
	wl_list_init(&frame->output_enable.link);
	wl_list_init(&frame->output_destroy.link);
	wl_list_init(&frame->buffer_destroy.link);

	if (output == NULL || !output->enabled) {
		goto error;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	if (!wlr_output_preferred_read_format(frame->output, &frame->format)) {
		wlr_log(WLR_ERROR,
			"Failed to capture output: no read format supported by renderer");
		goto error;
	}

	struct wlr_box buffer_box = {0};
	if (box == NULL) {
		buffer_box.width = output->width;
		buffer_box.height = output->height;
	} else {
		int ow, oh;
		wlr_output_effective_resolution(output, &ow, &oh);

		buffer_box = *box;

		wlr_box_transform(&buffer_box, &buffer_box, output->transform, ow, oh);
		buffer_box.x *= output->scale;
		buffer_box.y *= output->scale;
		buffer_box.width *= output->scale;
		buffer_box.height *= output->scale;
	}

	frame->box = buffer_box;
	frame->stride = 4 * buffer_box.width; // TODO: depends on read format

	zwlr_screencopy_frame_v1_send_buffer(frame->resource, frame->format,
		buffer_box.width, buffer_box.height, frame->stride);
	return;

error:
	zwlr_screencopy_frame_v1_send_failed(frame->resource);
	frame_destroy(frame);
}

static void manager_handle_capture_output(struct wl_client *wl_client,
		struct wl_resource *manager_resource, uint32_t id,
		int32_t overlay_cursor, struct wl_resource *output_resource) {
	struct wlr_screencopy_v1_client *client =
		client_from_resource(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	capture_output(wl_client, client, version, id, overlay_cursor, output,
		NULL);
}

static void manager_handle_capture_output_region(struct wl_client *wl_client,
		struct wl_resource *manager_resource, uint32_t id,
		int32_t overlay_cursor, struct wl_resource *output_resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_screencopy_v1_client *client =
		client_from_resource(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	struct wlr_box box = {
		.x = x,
		.y = y,
		.width = width,
		.height = height,
	};
	capture_output(wl_client, client, version, id, overlay_cursor, output,
		&box);
}

static void manager_handle_destroy(struct wl_client *wl_client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwlr_screencopy_manager_v1_interface manager_impl = {
	.capture_output = manager_handle_capture_output,
	.capture_output_region = manager_handle_capture_output_region,
	.destroy = manager_handle_destroy,
};

static void manager_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_screencopy_v1_client *client =
		client_from_resource(resource);
	client_unref(client);
}

static void manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_screencopy_manager_v1 *manager = data;

	struct wlr_screencopy_v1_client *client =
		calloc(1, sizeof(struct wlr_screencopy_v1_client));
	if (client == NULL) {
		goto failure;
	}

	struct wl_resource *resource = wl_resource_create(wl_client,
		&zwlr_screencopy_manager_v1_interface, version, id);
	if (resource == NULL) {
		goto failure;
	}

	client->ref = 1;
	client->manager = manager;
	wl_list_init(&client->damages);

	wl_resource_set_implementation(resource, &manager_impl, client,
		manager_handle_resource_destroy);

	return;
failure:
	free(client);
	wl_client_post_no_memory(wl_client);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_screencopy_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
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
	wl_list_init(&manager->frames);

	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
