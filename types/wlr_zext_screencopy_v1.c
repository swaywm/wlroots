#include <wlr/types/wlr_zext_screencopy_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/interface.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "util/signal.h"
#include "render/wlr_renderer.h"
#include "render/swapchain.h"

#include <stdlib.h>
#include <assert.h>
#include <drm_fourcc.h>
#include <limits.h>

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
	pixman_region32_fini(&surface->current_buffer.damage);
	pixman_region32_fini(&surface->staged_buffer.damage);
	pixman_region32_fini(&surface->staged_cursor_buffer.damage);
	pixman_region32_fini(&surface->current_cursor_buffer.damage);

	wl_list_remove(&surface->output_set_cursor.link);
	wl_list_remove(&surface->output_move_cursor.link);
	wl_list_remove(&surface->output_precommit.link);
	wl_list_remove(&surface->output_commit.link);
	wl_list_remove(&surface->output_destroy.link);

	if (surface->staged_buffer.resource) {
		wl_list_remove(&surface->staged_buffer.destroy.link);
	}

	if (surface->current_buffer.resource) {
		wl_list_remove(&surface->current_buffer.destroy.link);
	}

	if (surface->staged_cursor_buffer.resource) {
		wl_list_remove(&surface->staged_cursor_buffer.destroy.link);
	}

	if (surface->current_cursor_buffer.resource) {
		wl_list_remove(&surface->current_cursor_buffer.destroy.link);
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
		wl_container_of(listener, surface, staged_buffer.destroy);

	if (!surface->staged_buffer.resource) {
		return;
	}

	surface->staged_buffer.resource = NULL;
	wl_list_remove(&surface->staged_buffer.destroy.link);
}

static void surface_handle_committed_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, current_buffer.destroy);

	if (!surface->current_buffer.resource) {
		return;
	}

	surface->current_buffer.resource = NULL;
	wl_list_remove(&surface->current_buffer.destroy.link);
}

static void surface_handle_staged_cursor_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, staged_cursor_buffer.destroy);

	if (!surface->staged_cursor_buffer.resource) {
		return;
	}

	surface->staged_cursor_buffer.resource = NULL;
	wl_list_remove(&surface->staged_cursor_buffer.destroy.link);
}

static void surface_handle_committed_cursor_buffer_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, current_cursor_buffer.destroy);

	if (!surface->current_cursor_buffer.resource) {
		return;
	}

	surface->current_cursor_buffer.resource = NULL;
	wl_list_remove(&surface->current_cursor_buffer.destroy.link);
}

static void surface_attach_buffer(struct wl_client *client,
		struct wl_resource *surface_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	if (!surface) {
		return;
	}

	assert(buffer_resource);

	if (surface->staged_buffer.resource) {
		wl_list_remove(&surface->staged_buffer.destroy.link);
	}

	surface->staged_buffer.resource = buffer_resource;
	if (buffer_resource) {
		wl_list_init(&surface->staged_buffer.destroy.link);
		wl_resource_add_destroy_listener(buffer_resource,
				&surface->staged_buffer.destroy);
		surface->staged_buffer.destroy.notify =
			surface_handle_staged_buffer_destroy;
	}
}

static void surface_attach_cursor_buffer(struct wl_client *client,
		struct wl_resource *surface_resource,
		struct wl_resource *buffer_resource,
		const char *seat_name) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	if (!surface) {
		return;
	}

	// TODO: Do something with "seat_name"

	if (surface->staged_cursor_buffer.resource) {
		wl_list_remove(&surface->staged_cursor_buffer.destroy.link);
	}

	surface->staged_cursor_buffer.resource = buffer_resource;
	if (buffer_resource) {
		wl_resource_add_destroy_listener(buffer_resource,
				&surface->staged_cursor_buffer.destroy);
		surface->staged_cursor_buffer.destroy.notify =
			surface_handle_staged_cursor_buffer_destroy;
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

	pixman_region32_union_rect(&surface->staged_buffer.damage,
			&surface->staged_buffer.damage, x, y, width, height);
}

static void surface_damage_cursor_buffer(struct wl_client *client,
		struct wl_resource *surface_resource, const char *seat_name) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	if (!surface) {
		return;
	}

	// TODO: Do something with "seat_name"

	pixman_region32_union_rect(&surface->staged_cursor_buffer.damage,
			&surface->staged_cursor_buffer.damage, 0, 0,
			surface->cursor_width, surface->cursor_height);
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

	// Main buffer
	if (surface->current_buffer.resource) {
		wl_list_remove(&surface->current_buffer.destroy.link);
	}

	surface->current_buffer.resource = surface->staged_buffer.resource;
	surface->staged_buffer.resource = NULL;

	if (surface->current_buffer.resource) {
		wl_list_remove(&surface->staged_buffer.destroy.link);
		wl_resource_add_destroy_listener(
				surface->current_buffer.resource,
				&surface->current_buffer.destroy);
		surface->current_buffer.destroy.notify =
			surface_handle_committed_buffer_destroy;
	}

	pixman_region32_copy(&surface->current_buffer.damage,
			&surface->staged_buffer.damage);
	pixman_region32_clear(&surface->staged_buffer.damage);
	pixman_region32_intersect_rect(&surface->current_buffer.damage,
			&surface->current_buffer.damage, 0, 0, output->width,
			output->height);

	// Cursor buffer
	if (surface->current_cursor_buffer.resource) {
		wl_list_remove(&surface->current_cursor_buffer.destroy.link);
	}

	surface->current_cursor_buffer.resource =
		surface->staged_cursor_buffer.resource;
	surface->staged_cursor_buffer.resource = NULL;

	if (surface->current_cursor_buffer.resource) {
		wl_list_remove(&surface->staged_cursor_buffer.destroy.link);
		wl_resource_add_destroy_listener(
				surface->current_cursor_buffer.resource,
				&surface->current_cursor_buffer.destroy);
		surface->current_cursor_buffer.destroy.notify =
			surface_handle_committed_cursor_buffer_destroy;
	}

	pixman_region32_copy(&surface->current_cursor_buffer.damage,
			&surface->staged_cursor_buffer.damage);
	pixman_region32_clear(&surface->staged_cursor_buffer.damage);

	surface->options = options;

	if (options & ZEXT_SCREENCOPY_SURFACE_V1_OPTIONS_IMMEDIATE ||
			pixman_region32_not_empty(&surface->frame_damage) ||
			pixman_region32_not_empty(&surface->cursor_damage)) {
		wlr_output_schedule_frame(output);
	}

	surface->committed = true;
}

static void surface_handle_destroy(struct wl_client *client,
		struct wl_resource *surface_resource) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		surface_from_resource(surface_resource);
	surface_destroy(surface);
}

static const struct zext_screencopy_surface_v1_interface surface_impl = {
	.attach_buffer = surface_attach_buffer,
	.attach_cursor_buffer = surface_attach_cursor_buffer,
	.damage_buffer = surface_damage_buffer,
	.damage_cursor_buffer = surface_damage_cursor_buffer,
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
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, output_destroy);
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

static bool surface_is_cursor_visible(
		struct wlr_zext_screencopy_surface_v1 *surface)
{
	struct wlr_output *output = surface->output;
	struct wlr_output_cursor *cursor = output->hardware_cursor;

	return output->cursor_front_buffer && cursor && cursor->enabled &&
		cursor->visible;

	return output->cursor_front_buffer;
}

static void surface_advertise_cursor_formats(
		struct wlr_zext_screencopy_surface_v1 *surface) {
	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	struct wlr_buffer *buffer = output->cursor_front_buffer;
	if (!buffer) {
		return;
	}

	struct wlr_renderer *renderer = output->renderer;

	surface->cursor_wl_shm_format =
		get_buffer_preferred_read_format(buffer, renderer);
	surface->cursor_wl_shm_stride =
		buffer->width * 4; // TODO: This assumes things...
	surface->cursor_dmabuf_format = get_dmabuf_format(buffer);

	surface->cursor_width = buffer->width;
	surface->cursor_height = buffer->height;

	if (surface->cursor_wl_shm_format != DRM_FORMAT_INVALID) {
		assert(surface->cursor_wl_shm_stride);

		zext_screencopy_surface_v1_send_cursor_buffer_info(
				surface->resource, "default",
				ZEXT_SCREENCOPY_SURFACE_V1_BUFFER_TYPE_WL_SHM,
				surface->cursor_wl_shm_format,
				buffer->width, buffer->height,
				surface->cursor_wl_shm_stride);
	}

	if (surface->cursor_dmabuf_format != DRM_FORMAT_INVALID) {
		zext_screencopy_surface_v1_send_cursor_buffer_info(
				surface->resource, "default",
				ZEXT_SCREENCOPY_SURFACE_V1_BUFFER_TYPE_DMABUF,
				surface->cursor_dmabuf_format,
				buffer->width, buffer->height, 0);
	}

	pixman_region32_union_rect(&surface->cursor_damage,
			&surface->cursor_damage, 0, 0, surface->cursor_width,
			surface->cursor_height);
}

static void surface_advertise_buffer_formats(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_buffer *buffer) {
	struct wlr_output *output = surface_check_output(surface);
	if (!output) {
		return;
	}

	struct wlr_renderer *renderer = output->renderer;

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

	surface_advertise_cursor_formats(surface);

	zext_screencopy_surface_v1_send_init_done(surface->resource);

	if (surface_is_cursor_visible(surface)) {
		zext_screencopy_surface_v1_send_cursor_enter(surface->resource,
				"default");
		surface->have_cursor = true;
	}
}

static void surface_handle_output_commit_formats(
		struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_output_event_commit *event) {
	surface_advertise_buffer_formats(surface, event->buffer);
	surface->state = WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_READY;
}

static void surface_send_cursor_info(
		struct wlr_zext_screencopy_surface_v1 *surface) {
	if (!surface->current_cursor_buffer.resource ||
			!surface_is_cursor_visible(surface)) {
		return;
	}

	struct wlr_output *output = surface->output;
	struct wlr_output_cursor *cursor = output->hardware_cursor;

	bool have_damage = pixman_region32_not_empty(&surface->cursor_damage);

	zext_screencopy_surface_v1_send_cursor_info(surface->resource,
			"default", have_damage, cursor->x, cursor->y,
			cursor->width, cursor->height, cursor->hotspot_x,
			cursor->hotspot_y);
}

static void surface_send_transform(struct wlr_zext_screencopy_surface_v1 *surface) {
	enum wl_output_transform transform = surface->output->transform;
	zext_screencopy_surface_v1_send_transform(surface->resource, transform);
}

static bool surface_copy_wl_shm(struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_buffer *dst_buffer, struct wlr_shm_attributes *attr,
		struct wlr_buffer *src_buffer, uint32_t wl_shm_format,
		struct pixman_region32 *damage) {
	struct wlr_output *output = surface->output;
	struct wlr_renderer *renderer = output->renderer;

	if (dst_buffer->width < src_buffer->width ||
			dst_buffer->height < src_buffer->height) {
		return false;
	}

	uint32_t preferred_format = get_buffer_preferred_read_format(src_buffer,
			renderer);
	if (preferred_format != wl_shm_format) {
		return false;
	}

	int32_t width = src_buffer->width;
	int32_t height = src_buffer->height;
	uint8_t *dst_data = NULL;
	uint8_t *data = NULL;
	uint32_t format = DRM_FORMAT_INVALID;
	size_t dst_stride = 0;
	size_t stride = 0;

	if (!wlr_buffer_begin_data_ptr_access(dst_buffer,
			WLR_BUFFER_DATA_PTR_ACCESS_WRITE, (void**)&dst_data,
			&format, &dst_stride)) {
		return false;
	}

	bool use_scratch_buffer = dst_buffer->width != src_buffer->width;
	if (use_scratch_buffer) {
		stride = width * 4; // TODO: This assumes things
		data = malloc(height * stride);
	} else {
		data = dst_data;
		stride = dst_stride;
	}

	// TODO: Only copy damaged region
	uint32_t renderer_flags = 0;
	bool ok;
	ok = wlr_renderer_begin_with_buffer(renderer, src_buffer);
	ok = ok && wlr_renderer_read_pixels(renderer, format, &renderer_flags,
			stride, width, height, 0, 0, 0, 0, data);
	wlr_renderer_end(renderer);
	// TODO: if renderer_flags & WLR_RENDERER_READ_PIXELS_Y_INVERT:
	//    add vertical flip to transform

	if (use_scratch_buffer) {
		for (size_t y = 0; y < (size_t)height; ++y) {
			memcpy(dst_data + y * dst_stride, data + y * stride,
					stride);
			memset(dst_data + y * dst_stride + stride, 0,
					dst_stride - stride);
		}
		free(data);

		// Clear the rest of the destination buffer
		// TODO: Only do this if the rest is marked damaged
		memset(dst_data + height * dst_stride, 0,
				dst_stride * (dst_buffer->height - height));
	}

	wlr_buffer_end_data_ptr_access(dst_buffer);

	return ok;
}

static bool blit_dmabuf(struct wlr_renderer *renderer,
		struct wlr_buffer *dst_buffer,
		struct wlr_buffer *src_buffer, struct wlr_box *clip_box) {
	struct wlr_texture *src_tex =
		wlr_texture_from_buffer(renderer, src_buffer);
	if (!src_tex) {
		return false;
	}

	float mat[9];
	wlr_matrix_identity(mat);
	wlr_matrix_translate(mat, -1.0, 1.0);
	wlr_matrix_scale(mat, src_buffer->width, src_buffer->height);

	if (!wlr_renderer_begin_with_buffer(renderer, dst_buffer)) {
		goto error_renderer_begin;
	}

	wlr_renderer_scissor(renderer, clip_box);
	wlr_renderer_clear(renderer,  (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, src_tex, mat, 1.0f);
	wlr_renderer_scissor(renderer, NULL);

	wlr_renderer_end(renderer);

	wlr_texture_destroy(src_tex);
	return true;

error_renderer_begin:
	wlr_texture_destroy(src_tex);
	return false;
}

static bool surface_copy_dmabuf(struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_buffer *dst_buffer, struct wlr_dmabuf_attributes *attr,
		struct wlr_buffer *src_buffer, uint32_t format,
		struct pixman_region32 *damage) {
	struct wlr_output *output = surface->output;
	struct wlr_renderer *renderer = output->renderer;

	if (dst_buffer->width < src_buffer->width ||
			dst_buffer->height < src_buffer->height) {
		return false;
	}

	if (attr->format != format)
		return false;

	// TODO: More fine grained damage regions
	pixman_box32_t *extents = pixman_region32_extents(damage);
	struct wlr_box clip_box = {
		.x = extents->x1,
		.y = extents->y1,
		.width = extents->x2 - extents->x1,
		.height = extents->y2 - extents->y1,
	};

	return blit_dmabuf(renderer, dst_buffer, src_buffer, &clip_box);
}

static bool surface_copy(struct wlr_zext_screencopy_surface_v1 *surface,
		struct wlr_zext_screencopy_surface_v1_buffer *surface_buffer,
		struct wlr_buffer *src_buffer, uint32_t wl_shm_format,
		uint32_t dmabuf_format, struct pixman_region32 *damage) {
	if (!pixman_region32_not_empty(damage)) {
		// Nothing to do here...
		return true;
	}

	struct wlr_buffer *dst_buffer =
		wlr_buffer_from_resource(surface_buffer->resource);
	if (!dst_buffer) {
		goto failure;
	}

	struct wlr_shm_attributes shm_attr = { 0 };
	if (wlr_buffer_get_shm(dst_buffer, &shm_attr)) {
		if (!surface_copy_wl_shm(surface, dst_buffer, &shm_attr,
					src_buffer, wl_shm_format, damage)) {
			goto failure;
		}
	}

	struct wlr_dmabuf_attributes dmabuf_attr = { 0 };
	if (wlr_buffer_get_dmabuf(dst_buffer, &dmabuf_attr)) {
		if (!surface_copy_dmabuf(surface, dst_buffer, &dmabuf_attr,
					src_buffer, dmabuf_format, damage)) {
			goto failure;
		}
	}

	return true;

failure:
	if (surface->output && src_buffer == surface->output->cursor_front_buffer) {
		zext_screencopy_surface_v1_send_failed(surface->resource,
				ZEXT_SCREENCOPY_SURFACE_V1_FAILURE_REASON_INVALID_CURSOR_BUFFER);
	} else {
		zext_screencopy_surface_v1_send_failed(surface->resource,
				ZEXT_SCREENCOPY_SURFACE_V1_FAILURE_REASON_INVALID_MAIN_BUFFER);
	}
	return false;
}

static void surface_send_damage(struct wlr_zext_screencopy_surface_v1 *surface) {
	int n_rects = 0;
	struct pixman_box32 *rects =
		pixman_region32_rectangles(&surface->frame_damage, &n_rects);

	for (int i = 0; i < n_rects; ++i) {
		int damage_x = rects[i].x1;
		int damage_y = rects[i].y1;
		int damage_width = rects[i].x2 - rects[i].x1;
		int damage_height = rects[i].y2 - rects[i].y1;

		zext_screencopy_surface_v1_send_damage(surface->resource,
			damage_x, damage_y, damage_width, damage_height);
	}
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

	if (!surface->committed) {
		return;
	}

	if (surface->have_cursor && !surface_is_cursor_visible(surface)) {
		zext_screencopy_surface_v1_send_cursor_leave(surface->resource,
				"default");
		surface->have_cursor = false;
	} else if (!surface->have_cursor && surface_is_cursor_visible(surface)) {
		zext_screencopy_surface_v1_send_cursor_enter(surface->resource,
				"default");
		surface->have_cursor = true;
	}

	if (surface->current_buffer.resource) {
		struct pixman_region32 damage;
		pixman_region32_init(&damage);
		pixman_region32_union(&damage, &surface->frame_damage,
				&surface->current_buffer.damage);

		bool ok = surface_copy(surface, &surface->current_buffer,
				event->buffer, surface->wl_shm_format,
				surface->dmabuf_format, &damage);
		pixman_region32_fini(&damage);
		if (!ok) {
			// TODO: Raise some error, and destroy surface
		}
	}

	if (surface->current_cursor_buffer.resource &&
			surface_is_cursor_visible(surface)) {
		struct pixman_region32 damage;
		pixman_region32_init(&damage);
		pixman_region32_union(&damage, &surface->cursor_damage,
				&surface->current_cursor_buffer.damage);

		bool ok = surface_copy(surface, &surface->current_cursor_buffer,
				output->cursor_front_buffer,
				surface->cursor_wl_shm_format,
				surface->cursor_dmabuf_format, &damage);
		pixman_region32_fini(&damage);
		if (!ok) {
			// TODO: Raise some error, and destroy surface
		}
	}

	surface_send_transform(surface);
	surface_send_damage(surface);
	surface_send_cursor_info(surface);
	surface_send_commit_time(surface, event->when);
	zext_screencopy_surface_v1_send_ready(surface->resource);

	pixman_region32_clear(&surface->current_buffer.damage);
	pixman_region32_clear(&surface->current_cursor_buffer.damage);

	if (surface->current_buffer.resource) {
		wl_list_remove(&surface->current_buffer.destroy.link);
		pixman_region32_clear(&surface->frame_damage);
	}

	if (surface->current_cursor_buffer.resource) {
		wl_list_remove(&surface->current_cursor_buffer.destroy.link);
		pixman_region32_clear(&surface->cursor_damage);
	}

	surface->current_buffer.resource = NULL;
	surface->current_cursor_buffer.resource = NULL;
	surface->committed = false;
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

static void surface_handle_output_set_cursor(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, output_set_cursor);

	pixman_region32_union_rect(&surface->cursor_damage,
			&surface->cursor_damage, 0, 0,
			surface->cursor_width, surface->cursor_height);

	wlr_output_schedule_frame(surface->output);
}

static void surface_handle_output_move_cursor(struct wl_listener *listener,
		void *data) {
	struct wlr_zext_screencopy_surface_v1 *surface =
		wl_container_of(listener, surface, output_move_cursor);

	wlr_output_schedule_frame(surface->output);
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
	surface->cursor_wl_shm_format = DRM_FORMAT_INVALID;
	surface->cursor_dmabuf_format = DRM_FORMAT_INVALID;

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

	wl_list_init(&surface->output_precommit.link);
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

	wl_signal_add(&surface->output->events.set_cursor,
			&surface->output_set_cursor);
	surface->output_set_cursor.notify = surface_handle_output_set_cursor;

	wl_signal_add(&surface->output->events.move_cursor,
			&surface->output_move_cursor);
	surface->output_move_cursor.notify = surface_handle_output_move_cursor;

	pixman_region32_init(&surface->current_buffer.damage);
	pixman_region32_init(&surface->staged_buffer.damage);
	pixman_region32_init(&surface->current_cursor_buffer.damage);
	pixman_region32_init(&surface->staged_cursor_buffer.damage);
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
