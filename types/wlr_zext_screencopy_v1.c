#include <wlr/types/wlr_zext_screencopy_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>

#include "util/signal.h"
#include "render/wlr_renderer.h"

#include <stdlib.h>
#include <assert.h>
#include <drm_fourcc.h>

#include "screencopy-unstable-v1-protocol.h"

#define ZEXT_SCREENCOPY_MANAGER_VERSION 1

static const struct zext_screencopy_manager_v1_interface manager_impl;
static const struct zext_screencopy_surface_v1_interface surface_impl;

static struct wlr_zext_screencopy_surface_v1 *surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zext_screencopy_surface_v1_interface, &surface_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_zext_screencopy_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zext_screencopy_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void surface_destroy(struct wlr_zext_screencopy_surface_v1 *surface) {
	if (!surface) {
		return;
	}

	pixman_region32_fini(&surface->frame_damage);
	pixman_region32_fini(&surface->buffer_damage);
	pixman_region32_fini(&surface->staged_buffer_damage);

	wl_list_remove(&surface->output_precommit.link);
	wl_list_remove(&surface->output_commit.link);
	wl_list_remove(&surface->output_destroy.link);

	if (surface->staged_buffer_resource) {
		wl_list_remove(&surface->staged_buffer_destroy.link);
	}

	if (surface->buffer_resource) {
		wl_list_remove(&surface->buffer_destroy.link);
	}

	wl_resource_set_user_data(surface->resource, NULL);
	free(surface);
}

static struct wlr_output *surface_check_output(
		struct wlr_zext_screencopy_surface_v1 *surface) {
	if (!surface->output) {
		zext_screencopy_surface_v1_send_failed(surface->resource,
				ZEXT_SCREENCOPY_SURFACE_V1_FAILURE_REASON_OUTPUT_MISSING);
		surface_destroy(surface);
		return NULL;
	}

	if (!surface->output->enabled) {
		zext_screencopy_surface_v1_send_failed(surface->resource,
				ZEXT_SCREENCOPY_SURFACE_V1_FAILURE_REASON_OUTPUT_DISABLED);
		surface_destroy(surface);
		return NULL;
	}

	return surface->output;
}

static void surface_handle_staged_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, staged_buffer_destroy);
	surface->staged_buffer_resource = NULL;
	wl_list_remove(&surface->staged_buffer_destroy.link);
}

static void surface_handle_committed_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, buffer_destroy);
	surface->buffer_resource = NULL;
	wl_list_remove(&surface->buffer_destroy.link);
}

static void surface_attach_buffer(struct wl_client *client,
		struct wl_resource *surface_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	if (!surface) {
		return;
	}

	if (surface->staged_buffer_resource) {
		wl_list_remove(&surface->staged_buffer_destroy.link);
	}

	surface->staged_buffer_resource = buffer_resource;
	if (buffer_resource) {
		wl_resource_add_destroy_listener(buffer_resource,
				&surface->staged_buffer_destroy);
		surface->staged_buffer_destroy.notify =
			surface_handle_staged_buffer_destroy;
	}
}

static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *surface_resource, uint32_t x, uint32_t y,
		uint32_t width, uint32_t height) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	if (!surface) {
		return;
	}

	pixman_region32_union_rect(&surface->staged_buffer_damage,
			&surface->staged_buffer_damage, x, y, width, height);
}

static void surface_commit(struct wl_client *client,
		struct wl_resource *surface_resource, uint32_t options) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	if (!surface) {
		return;
	}

	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	surface->buffer_resource = surface->staged_buffer_resource;
	surface->staged_buffer_resource = NULL;

	wl_list_remove(&surface->staged_buffer_destroy.link);
	wl_resource_add_destroy_listener(surface->buffer_resource,
			&surface->buffer_destroy);
	surface->buffer_destroy.notify = surface_handle_committed_buffer_destroy;

	pixman_region32_copy(&surface->buffer_damage,
			&surface->staged_buffer_damage);
	pixman_region32_clear(&surface->staged_buffer_damage);
	pixman_region32_intersect_rect(&surface->buffer_damage,
			&surface->buffer_damage, 0, 0, output->width,
			output->height);

	surface->options = options;

	if (options & ZEXT_SCREENCOPY_SURFACE_V1_OPTIONS_IMMEDIATE) {
		wlr_output_schedule_frame(output);
	}
}

static void surface_handle_destroy(struct wl_client *client,
		struct wl_resource *surface_resource) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	surface_destroy(surface);
}

static const struct zext_screencopy_surface_v1_interface surface_impl = {
	.attach_buffer = surface_attach_buffer,
	.damage_buffer = surface_damage_buffer,
	.commit = surface_commit,
	.destroy = surface_handle_destroy,
};

static void surface_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(resource);
	surface_destroy(surface);
}

static void surface_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface = data;
	surface->output = NULL;
}

static uint32_t get_dmabuf_format(struct wlr_buffer *buffer) {
	struct wlr_dmabuf_attributes attr = { 0 };
	if (!wlr_buffer_get_dmabuf(buffer, &attr)) {
		return DRM_FORMAT_INVALID;
	}
	return attr.format;
}

static uint32_t get_buffer_preferred_read_format(struct wlr_buffer *buffer,
		struct wlr_renderer *renderer)
{
	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return DRM_FORMAT_INVALID;
	}

	if (!renderer_bind_buffer(renderer, buffer)) {
		return DRM_FORMAT_INVALID;
	}

	uint32_t format = renderer->impl->preferred_read_format(renderer);

	renderer_bind_buffer(renderer, NULL);

	return format;
}

static void surface_handle_output_precommit_formats(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_output_event_precommit *event) {
	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	// Nothing to do here...
}

static void surface_accumulate_frame_damage(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_output *output)
{
	struct pixman_region32 *region = &surface->frame_damage;

	if (output->pending.committed & WLR_OUTPUT_STATE_DAMAGE) {
		// If the compositor submitted damage, copy it over
		pixman_region32_union(region, region, &output->pending.damage);
		pixman_region32_intersect_rect(region, region, 0, 0,
			output->width, output->height);
	} else if (output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		// If the compositor did not submit damage but did submit a
		// buffer damage everything
		pixman_region32_union_rect(region, region, 0, 0,
			output->width, output->height);
	}
}

#if 0
static void surface_damage_whole(
		struct wlr_zext_screencopy_surface_v1 *surface) {
	struct pixman_region32 *region = &surface->frame_damage;
	struct wlr_output *output = surface->output;
	pixman_region32_union_rect(region, region, 0, 0, output->width,
			output->height);
}
#endif

static void surface_handle_output_precommit_ready(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_output_event_precommit *event) {
	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	surface_accumulate_frame_damage(surface, output);
}

static void surface_handle_output_precommit(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, output_precommit);
	struct wlr_output_event_precommit *event = data;

	assert(surface->output);

	switch(surface->state) {
	case WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_WAITING_FOR_BUFFER_FORMATS:
		surface_handle_output_precommit_formats(surface, event);
		break;
	case WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_READY:
		surface_handle_output_precommit_ready(surface, event);
		break;
	default:
		abort();
		break;
	}
}

static void surface_handle_output_commit_formats(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_output_event_commit *event) {
	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);

	struct wlr_buffer *buffer = event->buffer;
	assert(buffer);

	surface->wl_shm_format =
		get_buffer_preferred_read_format(buffer, renderer);
	surface->wl_shm_stride = buffer->width * 4; // TODO: This assumes things...

	surface->dmabuf_format = get_dmabuf_format(buffer);

	if (surface->wl_shm_format != DRM_FORMAT_INVALID) {
		assert(surface->wl_shm_stride);

		zext_screencopy_surface_v1_send_buffer_info(surface->resource,
				ZEXT_SCREENCOPY_SURFACE_V1_BUFFER_TYPE_WL_SHM,
				surface->wl_shm_format, output->width,
				output->height, surface->wl_shm_stride);
	}

	if (surface->dmabuf_format != DRM_FORMAT_INVALID) {
		zext_screencopy_surface_v1_send_buffer_info(surface->resource,
				ZEXT_SCREENCOPY_SURFACE_V1_BUFFER_TYPE_DMABUF,
				surface->dmabuf_format, output->width,
				output->height, 0);
	}

	zext_screencopy_surface_v1_send_init_done(surface->resource);
	surface->state = WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_READY;
}

#if 0
static void surface_send_cursor_info(
		struct wlr_zext_screencopy_surface_v1 *surface) {
	struct wlr_output *output = surface->output;
	struct wlr_output_cursor *cursor = output->hardware_cursor;

	zext_screencopy_surface_v1_send_cursor_info(surface->resource,
			"default", 1, cursor->x, cursor->y, cursor->hotspot_x,
			cursor->hotspot_y);
}
#endif

static void surface_send_transform(struct wlr_zext_screencopy_surface_v1 *surface) {
	enum wl_output_transform transform = surface->output->transform;
	zext_screencopy_surface_v1_send_transform(surface->resource, transform);
}

static bool surface_copy_wl_shm(struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_buffer *dst_buffer, struct wlr_shm_attributes *attr,
		struct wlr_buffer *src_buffer) {
	struct wlr_output *output = surface->output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);

	if (dst_buffer->width != src_buffer->width ||
			dst_buffer->height != src_buffer->height) {
		return false;
	}

	uint32_t preferred_format = get_buffer_preferred_read_format(src_buffer,
			renderer);
	if (preferred_format != surface->wl_shm_format) {
		return false;
	}

	int32_t width = dst_buffer->width;
	int32_t height = dst_buffer->height;
	void *data = NULL;
	uint32_t format = DRM_FORMAT_INVALID;
	size_t stride = 0;

	if (!wlr_buffer_begin_data_ptr_access(dst_buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &data, &format,
			&stride)) {
		return false;
	}

	uint32_t renderer_flags = 0;
	bool ok;
	ok = wlr_renderer_begin_with_buffer(renderer, src_buffer);
	ok = ok && wlr_renderer_read_pixels(renderer, format, &renderer_flags,
			stride, width, height, 0, 0, 0, 0, data);
	wlr_renderer_end(renderer);
	// TODO: if renderer_flags & WLR_RENDERER_READ_PIXELS_Y_INVERT:
	//    add vertical flip to transform
	wlr_buffer_end_data_ptr_access(dst_buffer);

	return ok;
}

static bool blit_dmabuf(struct wlr_renderer *renderer,
		struct wlr_buffer *dst_buffer,
		struct wlr_buffer *src_buffer) {
	struct wlr_texture *src_tex =
		wlr_texture_from_buffer(renderer, src_buffer);
	if (!src_tex) {
		return false;
	}

	float mat[9];
	wlr_matrix_identity(mat);
	wlr_matrix_scale(mat, dst_buffer->width, dst_buffer->height);

	if (!wlr_renderer_begin_with_buffer(renderer, dst_buffer)) {
		goto error_renderer_begin;
	}

	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, src_tex, mat, 1.0f);

	wlr_renderer_end(renderer);

	wlr_texture_destroy(src_tex);
	return true;

error_renderer_begin:
	wlr_texture_destroy(src_tex);
	return false;
}

static bool surface_copy_dmabuf(struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_buffer *dst_buffer, struct wlr_dmabuf_attributes *attr,
		struct wlr_buffer *src_buffer) {
	struct wlr_output *output = surface->output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);

	if (dst_buffer->width != src_buffer->width ||
			dst_buffer->height != src_buffer->height) {
		return false;
	}

	if (attr->format != surface->dmabuf_format)
		return false;

	return blit_dmabuf(renderer, dst_buffer, src_buffer);
}

static bool surface_copy(struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_buffer *src_buffer) {
	struct wlr_buffer *dst_buffer =
		wlr_buffer_from_resource(surface->buffer_resource);
	if (!dst_buffer) {
		goto failure;
	}

	struct wlr_shm_attributes shm_attr = { 0 };
	if (wlr_buffer_get_shm(dst_buffer, &shm_attr)) {
		if (!surface_copy_wl_shm(surface, dst_buffer, &shm_attr,
					src_buffer)) {
			goto failure;
		}
	}

	struct wlr_dmabuf_attributes dmabuf_attr = { 0 };
	if (wlr_buffer_get_dmabuf(dst_buffer, &dmabuf_attr)) {
		if (!surface_copy_dmabuf(surface, dst_buffer, &dmabuf_attr,
					src_buffer)) {
			goto failure;
		}
	}

	return true;

failure:
	zext_screencopy_surface_v1_send_failed(surface->resource,
			ZEXT_SCREENCOPY_SURFACE_V1_FAILURE_REASON_INVALID_BUFFER);
	surface_destroy(surface);
	return false;
}

static void surface_send_damage(struct wlr_zext_screencopy_surface_v1 *surface) {
	// TODO: send fine-grained damage events
	struct pixman_box32 *damage_box =
		pixman_region32_extents(&surface->frame_damage);

	int damage_x = damage_box->x1;
	int damage_y = damage_box->y1;
	int damage_width = damage_box->x2 - damage_box->x1;
	int damage_height = damage_box->y2 - damage_box->y1;

	zext_screencopy_surface_v1_send_damage(surface->resource,
		damage_x, damage_y, damage_width, damage_height);
}

static void surface_send_commit_time(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct timespec *when)
{
	time_t tv_sec = when->tv_sec;
	uint32_t tv_sec_hi = (sizeof(tv_sec) > 4) ? tv_sec >> 32 : 0;
	uint32_t tv_sec_lo = tv_sec & 0xFFFFFFFF;
	zext_screencopy_surface_v1_send_commit_time(surface->resource,
			tv_sec_hi, tv_sec_lo, when->tv_nsec);
}

static void surface_handle_output_commit_ready(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_output_event_commit *event) {
	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	if (!(event->committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}

	if (!surface->buffer_resource) {
		return;
	}

	if (!surface_copy(surface, event->buffer)) {
		return;
	}

	// TODO
	/*
	if (output->cursor_buffer) {
		if (!surface_copy_cursor(surface, output->cursor_front_buffer)) {
			return;
		}

		surface_send_cursor_info(surface);
	}
	*/

	surface_send_transform(surface);
	surface_send_damage(surface);
	surface_send_commit_time(surface, event->when);
	zext_screencopy_surface_v1_send_ready(surface->resource);

	pixman_region32_clear(&surface->frame_damage);
	pixman_region32_clear(&surface->buffer_damage);

	wl_list_remove(&surface->buffer_destroy.link);
	surface->buffer_resource = NULL;
}

static void surface_handle_output_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, output_commit);
	struct wlr_output_event_commit *event = data;

	assert(surface->output);

	switch(surface->state) {
	case WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_WAITING_FOR_BUFFER_FORMATS:
		surface_handle_output_commit_formats(surface, event);
		break;
	case WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_READY:
		surface_handle_output_commit_ready(surface, event);
		break;
	default:
		abort();
		break;
	}
}

static void capture_output(struct wl_client *client, uint32_t version,
		struct wlr_zext_screencopy_manager_v1 *manager,
		uint32_t surface_id, struct wlr_output *output) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		calloc(1, sizeof(struct wlr_zext_screencopy_surface_v1));
	if (!surface) {
		wl_client_post_no_memory(client);
		return;
	}

	surface->wl_shm_format = DRM_FORMAT_INVALID;
	surface->dmabuf_format = DRM_FORMAT_INVALID;

	surface->resource = wl_resource_create(client,
			&zext_screencopy_surface_v1_interface, version,
			surface_id);
	if (!surface->resource) {
		free(surface);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(surface->resource, &surface_impl,
			surface, surface_handle_resource_destroy);

	wl_list_init(&surface->output_commit.link);
	wl_list_init(&surface->output_destroy.link);

	surface->output = output;

	wl_signal_add(&surface->output->events.destroy,
			&surface->output_destroy);
	surface->output_destroy.notify = surface_handle_output_destroy;

	wl_signal_add(&surface->output->events.precommit,
			&surface->output_precommit);
	surface->output_precommit.notify = surface_handle_output_precommit;

	wl_signal_add(&surface->output->events.commit,
			&surface->output_commit);
	surface->output_commit.notify = surface_handle_output_commit;

	pixman_region32_init(&surface->buffer_damage);
	pixman_region32_init(&surface->staged_buffer_damage);
	pixman_region32_init(&surface->frame_damage);

	pixman_region32_union_rect(&surface->frame_damage,
			&surface->frame_damage, 0, 0, output->width,
			output->height);

	// We need a new frame to check the buffer formats
	wlr_output_schedule_frame(output);
}

static void manager_capture_output(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t surface_id,
		struct wl_resource *output_resource) {
	struct wlr_zext_screencopy_manager_v1 *manager =
		manager_from_resource(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	capture_output(client, version, manager, surface_id, output);
}

static const struct zext_screencopy_manager_v1_interface manager_impl = {
	.capture_output = manager_capture_output,
};

static void manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wl_resource *resource = wl_resource_create(client,
			&zext_screencopy_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_zext_screencopy_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_zext_screencopy_manager_v1 *wlr_zext_screencopy_manager_v1_create(
		struct wl_display *display) {
	struct wlr_zext_screencopy_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_zext_screencopy_manager_v1));
	if (!manager) {
		return NULL;
	}

	manager->global = wl_global_create(display,
		&zext_screencopy_manager_v1_interface,
		ZEXT_SCREENCOPY_MANAGER_VERSION, manager, manager_bind);
	if (!manager->global) {
		free(manager);
		return NULL;
	}

	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
