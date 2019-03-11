#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "util/signal.h"

#define OUTPUT_VERSION 3

static void output_send_to_resource(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	const uint32_t version = wl_resource_get_version(resource);
	if (version >= WL_OUTPUT_GEOMETRY_SINCE_VERSION) {
		wl_output_send_geometry(resource, 0, 0,
			output->phys_width, output->phys_height, output->subpixel,
			output->make, output->model, output->transform);
	}
	if (version >= WL_OUTPUT_MODE_SINCE_VERSION) {
		struct wlr_output_mode *mode;
		wl_list_for_each(mode, &output->modes, link) {
			uint32_t flags = mode->flags & WL_OUTPUT_MODE_PREFERRED;
			if (output->current_mode == mode) {
				flags |= WL_OUTPUT_MODE_CURRENT;
			}
			wl_output_send_mode(resource, flags, mode->width, mode->height,
				mode->refresh);
		}

		if (wl_list_length(&output->modes) == 0) {
			// Output has no mode, send the current width/height
			wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT,
				output->width, output->height, output->refresh);
		}
	}
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
		wl_output_send_scale(resource, (uint32_t)ceil(output->scale));
	}
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(resource);
	}
}

static void output_send_current_mode_to_resource(
		struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	const uint32_t version = wl_resource_get_version(resource);
	if (version < WL_OUTPUT_MODE_SINCE_VERSION) {
		return;
	}
	if (output->current_mode != NULL) {
		struct wlr_output_mode *mode = output->current_mode;
		uint32_t flags = mode->flags & WL_OUTPUT_MODE_PREFERRED;
		wl_output_send_mode(resource, flags | WL_OUTPUT_MODE_CURRENT,
			mode->width, mode->height, mode->refresh);
	} else {
		// Output has no mode
		wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, output->width,
			output->height, output->refresh);
	}
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(resource);
	}
}

static void output_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void output_handle_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_output_interface output_impl = {
	.release = output_handle_release,
};

static void output_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_output *output = data;

	struct wl_resource *resource = wl_resource_create(wl_client,
		&wl_output_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(resource, &output_impl, output,
		output_handle_resource_destroy);
	wl_list_insert(&output->resources, wl_resource_get_link(resource));
	output_send_to_resource(resource);
}

void wlr_output_create_global(struct wlr_output *output) {
	if (output->global != NULL) {
		return;
	}
	output->global = wl_global_create(output->display,
		&wl_output_interface, OUTPUT_VERSION, output, output_bind);
	if (output->global == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wl_output global");
	}
}

void wlr_output_destroy_global(struct wlr_output *output) {
	if (output->global == NULL) {
		return;
	}
	// Make all output resources inert
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &output->resources) {
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}
	wl_global_destroy(output->global);
	output->global = NULL;
}

void wlr_output_update_enabled(struct wlr_output *output, bool enabled) {
	if (output->enabled == enabled) {
		return;
	}

	output->enabled = enabled;
	wlr_signal_emit_safe(&output->events.enable, output);
}

static void output_update_matrix(struct wlr_output *output) {
	wlr_matrix_projection(output->transform_matrix, output->width,
		output->height, output->transform);
}

bool wlr_output_enable(struct wlr_output *output, bool enable) {
	if (output->enabled == enable) {
		return true;
	}

	if (output->impl->enable) {
		return output->impl->enable(output, enable);
	}
	return false;
}

bool wlr_output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	if (!output->impl || !output->impl->set_mode) {
		return false;
	}
	if (output->current_mode == mode) {
		return true;
	}
	return output->impl->set_mode(output, mode);
}

bool wlr_output_set_custom_mode(struct wlr_output *output, int32_t width,
		int32_t height, int32_t refresh) {
	if (!output->impl || !output->impl->set_custom_mode) {
		return false;
	}
	if (output->width == width && output->height == height &&
			output->refresh == refresh) {
		return true;
	}
	return output->impl->set_custom_mode(output, width, height, refresh);
}

void wlr_output_update_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	output->current_mode = mode;
	if (mode != NULL) {
		wlr_output_update_custom_mode(output, mode->width, mode->height,
			mode->refresh);
	} else {
		wlr_output_update_custom_mode(output, 0, 0, 0);
	}
}

void wlr_output_update_custom_mode(struct wlr_output *output, int32_t width,
		int32_t height, int32_t refresh) {
	if (output->width == width && output->height == height &&
			output->refresh == refresh) {
		return;
	}

	output->width = width;
	output->height = height;
	output_update_matrix(output);

	output->refresh = refresh;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		output_send_current_mode_to_resource(resource);
	}

	wlr_signal_emit_safe(&output->events.mode, output);
}

void wlr_output_set_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->impl->transform(output, transform);
	output_update_matrix(output);

	// TODO: only send geometry and done
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		output_send_to_resource(resource);
	}

	wlr_signal_emit_safe(&output->events.transform, output);
}

void wlr_output_set_scale(struct wlr_output *output, float scale) {
	if (output->scale == scale) {
		return;
	}

	output->scale = scale;

	// TODO: only send mode and done
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		output_send_to_resource(resource);
	}

	wlr_signal_emit_safe(&output->events.scale, output);
}

void wlr_output_set_subpixel(struct wlr_output *output, enum wl_output_subpixel subpixel) {
	if (output->subpixel == subpixel) {
		return;
	}

	output->subpixel = subpixel;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		output_send_to_resource(resource);
	}
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output *output =
		wl_container_of(listener, output, display_destroy);
	wlr_output_destroy_global(output);
}

void wlr_output_init(struct wlr_output *output, struct wlr_backend *backend,
		const struct wlr_output_impl *impl, struct wl_display *display) {
	assert(impl->make_current && impl->swap_buffers && impl->transform);
	if (impl->set_cursor || impl->move_cursor) {
		assert(impl->set_cursor && impl->move_cursor);
	}
	output->backend = backend;
	output->impl = impl;
	output->display = display;
	wl_list_init(&output->modes);
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	output->scale = 1;
	wl_list_init(&output->cursors);
	wl_list_init(&output->resources);
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.needs_swap);
	wl_signal_init(&output->events.swap_buffers);
	wl_signal_init(&output->events.present);
	wl_signal_init(&output->events.enable);
	wl_signal_init(&output->events.mode);
	wl_signal_init(&output->events.scale);
	wl_signal_init(&output->events.transform);
	wl_signal_init(&output->events.destroy);
	pixman_region32_init(&output->damage);

	const char *no_hardware_cursors = getenv("WLR_NO_HARDWARE_CURSORS");
	if (no_hardware_cursors != NULL && strcmp(no_hardware_cursors, "1") == 0) {
		wlr_log(WLR_DEBUG,
			"WLR_NO_HARDWARE_CURSORS set, forcing software cursors");
		output->software_cursor_locks = 1;
	}

	output->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &output->display_destroy);

	output->frame_pending = true;
}

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) {
		return;
	}

	wl_list_remove(&output->display_destroy.link);
	wlr_output_destroy_global(output);

	wlr_signal_emit_safe(&output->events.destroy, output);

	// The backend is responsible for free-ing the list of modes

	struct wlr_output_cursor *cursor, *tmp_cursor;
	wl_list_for_each_safe(cursor, tmp_cursor, &output->cursors, link) {
		wlr_output_cursor_destroy(cursor);
	}

	if (output->idle_frame != NULL) {
		wl_event_source_remove(output->idle_frame);
	}

	pixman_region32_fini(&output->damage);

	if (output->impl && output->impl->destroy) {
		output->impl->destroy(output);
	} else {
		free(output);
	}
}

void wlr_output_transformed_resolution(struct wlr_output *output,
		int *width, int *height) {
	if (output->transform % 2 == 0) {
		*width = output->width;
		*height = output->height;
	} else {
		*width = output->height;
		*height = output->width;
	}
}

void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height) {
	wlr_output_transformed_resolution(output, width, height);
	*width /= output->scale;
	*height /= output->scale;
}

bool wlr_output_make_current(struct wlr_output *output, int *buffer_age) {
	return output->impl->make_current(output, buffer_age);
}

bool wlr_output_preferred_read_format(struct wlr_output *output,
		enum wl_shm_format *fmt) {
	if (!wlr_output_make_current(output, NULL)) {
		return false;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return false;
	}
	*fmt = renderer->impl->preferred_read_format(renderer);
	return true;
}

bool wlr_output_swap_buffers(struct wlr_output *output, struct timespec *when,
		pixman_region32_t *damage) {
	if (output->frame_pending) {
		wlr_log(WLR_ERROR, "Tried to swap buffers when a frame is pending");
		return false;
	}
	if (output->idle_frame != NULL) {
		wl_event_source_remove(output->idle_frame);
		output->idle_frame = NULL;
	}

	struct timespec now;
	if (when == NULL) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		when = &now;
	}

	struct wlr_output_event_swap_buffers event = {
		.output = output,
		.when = when,
		.damage = damage,
	};
	wlr_signal_emit_safe(&output->events.swap_buffers, &event);

	pixman_region32_t render_damage;
	pixman_region32_init(&render_damage);
	pixman_region32_union_rect(&render_damage, &render_damage, 0, 0,
		output->width, output->height);
	if (damage != NULL) {
		// Damage tracking supported
		pixman_region32_intersect(&render_damage, &render_damage, damage);
	}

	if (!output->impl->swap_buffers(output, damage ? &render_damage : NULL)) {
		pixman_region32_fini(&render_damage);
		return false;
	}

	pixman_region32_fini(&render_damage);

	struct wlr_output_cursor *cursor;
	wl_list_for_each(cursor, &output->cursors, link) {
		if (!cursor->enabled || !cursor->visible || cursor->surface == NULL) {
			continue;
		}
		wlr_surface_send_frame_done(cursor->surface, when);
	}

	output->frame_pending = true;
	output->needs_swap = false;
	pixman_region32_clear(&output->damage);
	return true;
}

void wlr_output_send_frame(struct wlr_output *output) {
	output->frame_pending = false;
	wlr_signal_emit_safe(&output->events.frame, output);
}

static void schedule_frame_handle_idle_timer(void *data) {
	struct wlr_output *output = data;
	output->idle_frame = NULL;
	if (!output->frame_pending && output->impl->schedule_frame) {
		// Ask the backend to send a frame event when appropriate
		if (output->impl->schedule_frame(output)) {
			output->frame_pending = true;
		}
	}
}

void wlr_output_schedule_frame(struct wlr_output *output) {
	if (output->frame_pending || output->idle_frame != NULL) {
		return;
	}

	// We're using an idle timer here in case a buffer swap happens right after
	// this function is called
	struct wl_event_loop *ev = wl_display_get_event_loop(output->display);
	output->idle_frame =
		wl_event_loop_add_idle(ev, schedule_frame_handle_idle_timer, output);
}

void wlr_output_send_present(struct wlr_output *output,
		struct wlr_output_event_present *event) {
	struct wlr_output_event_present _event = {0};
	if (event == NULL) {
		event = &_event;
	}

	event->output = output;

	struct timespec now;
	if (event->when == NULL) {
		clockid_t clock = wlr_backend_get_presentation_clock(output->backend);
		errno = 0;
		if (clock_gettime(clock, &now) != 0) {
			wlr_log_errno(WLR_ERROR, "failed to send output present event: "
				"failed to read clock");
			return;
		}
		event->when = &now;
	}

	wlr_signal_emit_safe(&output->events.present, event);
}

bool wlr_output_set_gamma(struct wlr_output *output, size_t size,
		const uint16_t *r, const uint16_t *g, const uint16_t *b) {
	if (!output->impl->set_gamma) {
		return false;
	}
	return output->impl->set_gamma(output, size, r, g, b);
}

size_t wlr_output_get_gamma_size(struct wlr_output *output) {
	if (!output->impl->get_gamma_size) {
		return 0;
	}
	return output->impl->get_gamma_size(output);
}

bool wlr_output_export_dmabuf(struct wlr_output *output,
		struct wlr_dmabuf_attributes *attribs) {
	if (!output->impl->export_dmabuf) {
		return false;
	}
	return output->impl->export_dmabuf(output, attribs);
}

void wlr_output_update_needs_swap(struct wlr_output *output) {
	output->needs_swap = true;
	wlr_signal_emit_safe(&output->events.needs_swap, output);
}

void wlr_output_damage_whole(struct wlr_output *output) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	pixman_region32_union_rect(&output->damage, &output->damage, 0, 0,
		width, height);
	wlr_output_update_needs_swap(output);
}

struct wlr_output *wlr_output_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_output_interface,
		&output_impl));
	return wl_resource_get_user_data(resource);
}

static void output_cursor_damage_whole(struct wlr_output_cursor *cursor);

void wlr_output_lock_software_cursors(struct wlr_output *output, bool lock) {
	if (lock) {
		++output->software_cursor_locks;
	} else {
		assert(output->software_cursor_locks > 0);
		--output->software_cursor_locks;
	}
	wlr_log(WLR_DEBUG, "%s hardware cursors on output '%s' (locks: %d)",
		lock ? "Disabling" : "Enabling", output->name,
		output->software_cursor_locks);

	if (output->software_cursor_locks > 0 && output->hardware_cursor != NULL) {
		assert(output->impl->set_cursor);
		output->impl->set_cursor(output, NULL, 1,
			WL_OUTPUT_TRANSFORM_NORMAL, 0, 0, true);
		output_cursor_damage_whole(output->hardware_cursor);
		output->hardware_cursor = NULL;
	}

	// If it's possible to use hardware cursors again, don't switch immediately
	// since a recorder is likely to lock software cursors for the next frame
	// again.
}

static void output_scissor(struct wlr_output *output, pixman_box32_t *rect) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};

	int ow, oh;
	wlr_output_transformed_resolution(output, &ow, &oh);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);
	wlr_box_transform(&box, &box, transform, ow, oh);

	wlr_renderer_scissor(renderer, &box);
}

static void output_cursor_get_box(struct wlr_output_cursor *cursor,
	struct wlr_box *box);

static void output_cursor_render(struct wlr_output_cursor *cursor,
		pixman_region32_t *damage) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(cursor->output->backend);
	assert(renderer);

	struct wlr_texture *texture = cursor->texture;
	if (cursor->surface != NULL) {
		texture = wlr_surface_get_texture(cursor->surface);
	}
	if (texture == NULL) {
		return;
	}

	struct wlr_box box;
	output_cursor_get_box(cursor, &box);

	pixman_region32_t surface_damage;
	pixman_region32_init(&surface_damage);
	pixman_region32_union_rect(&surface_damage, &surface_damage, box.x, box.y,
		box.width, box.height);
	pixman_region32_intersect(&surface_damage, &surface_damage, damage);
	if (!pixman_region32_not_empty(&surface_damage)) {
		goto surface_damage_finish;
	}

	float matrix[9];
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		cursor->output->transform_matrix);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&surface_damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		output_scissor(cursor->output, &rects[i]);
		wlr_render_texture_with_matrix(renderer, texture, matrix, 1.0f);
	}
	wlr_renderer_scissor(renderer, NULL);

surface_damage_finish:
	pixman_region32_fini(&surface_damage);
}

void wlr_output_render_software_cursors(struct wlr_output *output,
		pixman_region32_t *damage) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	pixman_region32_t render_damage;
	pixman_region32_init(&render_damage);
	pixman_region32_union_rect(&render_damage, &render_damage, 0, 0,
		width, height);
	if (damage != NULL) {
		// Damage tracking supported
		pixman_region32_intersect(&render_damage, &render_damage, damage);
	}

	if (pixman_region32_not_empty(&render_damage)) {
		struct wlr_output_cursor *cursor;
		wl_list_for_each(cursor, &output->cursors, link) {
			if (!cursor->enabled || !cursor->visible ||
					output->hardware_cursor == cursor) {
				continue;
			}
			output_cursor_render(cursor, &render_damage);
		}
	}

	pixman_region32_fini(&render_damage);
}


/**
 * Returns the cursor box, scaled for its output.
 */
static void output_cursor_get_box(struct wlr_output_cursor *cursor,
		struct wlr_box *box) {
	box->x = cursor->x - cursor->hotspot_x;
	box->y = cursor->y - cursor->hotspot_y;
	box->width = cursor->width;
	box->height = cursor->height;
}

static void output_cursor_damage_whole(struct wlr_output_cursor *cursor) {
	struct wlr_box box;
	output_cursor_get_box(cursor, &box);
	pixman_region32_union_rect(&cursor->output->damage, &cursor->output->damage,
		box.x, box.y, box.width, box.height);
	wlr_output_update_needs_swap(cursor->output);
}

static void output_cursor_reset(struct wlr_output_cursor *cursor) {
	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}
	if (cursor->surface != NULL) {
		wl_list_remove(&cursor->surface_commit.link);
		wl_list_remove(&cursor->surface_destroy.link);
		cursor->surface = NULL;
	}
}

static void output_cursor_update_visible(struct wlr_output_cursor *cursor) {
	struct wlr_box output_box;
	output_box.x = output_box.y = 0;
	wlr_output_transformed_resolution(cursor->output, &output_box.width,
		&output_box.height);

	struct wlr_box cursor_box;
	output_cursor_get_box(cursor, &cursor_box);

	struct wlr_box intersection;
	bool visible =
		wlr_box_intersection(&intersection, &output_box, &cursor_box);

	if (cursor->surface != NULL) {
		if (cursor->visible && !visible) {
			wlr_surface_send_leave(cursor->surface, cursor->output);
		}
		if (!cursor->visible && visible) {
			wlr_surface_send_enter(cursor->surface, cursor->output);
		}
	}

	cursor->visible = visible;
}

static bool output_cursor_attempt_hardware(struct wlr_output_cursor *cursor) {
	int32_t scale = cursor->output->scale;
	enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
	struct wlr_texture *texture = cursor->texture;
	if (cursor->surface != NULL) {
		texture = wlr_surface_get_texture(cursor->surface);
		scale = cursor->surface->current.scale;
		transform = cursor->surface->current.transform;
	}

	if (cursor->output->software_cursor_locks > 0) {
		return false;
	}

	struct wlr_output_cursor *hwcur = cursor->output->hardware_cursor;
	if (cursor->output->impl->set_cursor && (hwcur == NULL || hwcur == cursor)) {
		// If the cursor was hidden or was a software cursor, the hardware
		// cursor position is outdated
		assert(cursor->output->impl->move_cursor);
		cursor->output->impl->move_cursor(cursor->output,
			(int)cursor->x, (int)cursor->y);
		if (cursor->output->impl->set_cursor(cursor->output, texture,
				scale, transform, cursor->hotspot_x, cursor->hotspot_y, true)) {
			cursor->output->hardware_cursor = cursor;
			return true;
		}
	}
	return false;
}

bool wlr_output_cursor_set_image(struct wlr_output_cursor *cursor,
		const uint8_t *pixels, int32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(cursor->output->backend);
	assert(renderer);

	output_cursor_reset(cursor);

	cursor->width = width;
	cursor->height = height;
	cursor->hotspot_x = hotspot_x;
	cursor->hotspot_y = hotspot_y;
	output_cursor_update_visible(cursor);

	wlr_texture_destroy(cursor->texture);
	cursor->texture = NULL;

	cursor->enabled = false;
	if (pixels != NULL) {
		cursor->texture = wlr_texture_from_pixels(renderer,
			WL_SHM_FORMAT_ARGB8888, stride, width, height, pixels);
		if (cursor->texture == NULL) {
			return false;
		}
		cursor->enabled = true;
	}

	if (output_cursor_attempt_hardware(cursor)) {
		return true;
	}

	wlr_log(WLR_DEBUG, "Falling back to software cursor on output '%s'",
		cursor->output->name);
	output_cursor_damage_whole(cursor);
	return true;
}

static void output_cursor_commit(struct wlr_output_cursor *cursor,
		bool update_hotspot) {
	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}

	struct wlr_surface *surface = cursor->surface;
	assert(surface != NULL);

	// Some clients commit a cursor surface with a NULL buffer to hide it.
	cursor->enabled = wlr_surface_has_buffer(surface);
	cursor->width = surface->current.width * cursor->output->scale;
	cursor->height = surface->current.height * cursor->output->scale;
	output_cursor_update_visible(cursor);
	if (update_hotspot) {
		cursor->hotspot_x -= surface->current.dx * cursor->output->scale;
		cursor->hotspot_y -= surface->current.dy * cursor->output->scale;
	}

	if (output_cursor_attempt_hardware(cursor)) {
		return;
	}

	// Fallback to software cursor
	output_cursor_damage_whole(cursor);
}

static void output_cursor_handle_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_output_cursor *cursor =
		wl_container_of(listener, cursor, surface_commit);
	output_cursor_commit(cursor, true);
}

static void output_cursor_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_cursor *cursor = wl_container_of(listener, cursor,
		surface_destroy);
	output_cursor_reset(cursor);
}

void wlr_output_cursor_set_surface(struct wlr_output_cursor *cursor,
		struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y) {
	hotspot_x *= cursor->output->scale;
	hotspot_y *= cursor->output->scale;

	if (surface && surface == cursor->surface) {
		// Only update the hotspot: surface hasn't changed

		if (cursor->output->hardware_cursor != cursor) {
			output_cursor_damage_whole(cursor);
		}
		cursor->hotspot_x = hotspot_x;
		cursor->hotspot_y = hotspot_y;
		if (cursor->output->hardware_cursor != cursor) {
			output_cursor_damage_whole(cursor);
		} else {
			assert(cursor->output->impl->set_cursor);
			cursor->output->impl->set_cursor(cursor->output, NULL,
				1, WL_OUTPUT_TRANSFORM_NORMAL, hotspot_x, hotspot_y, false);
		}
		return;
	}

	output_cursor_reset(cursor);

	cursor->surface = surface;
	cursor->hotspot_x = hotspot_x;
	cursor->hotspot_y = hotspot_y;

	if (surface != NULL) {
		wl_signal_add(&surface->events.commit, &cursor->surface_commit);
		wl_signal_add(&surface->events.destroy, &cursor->surface_destroy);

		cursor->visible = false;
		output_cursor_commit(cursor, false);
	} else {
		cursor->enabled = false;
		cursor->width = 0;
		cursor->height = 0;

		if (cursor->output->hardware_cursor == cursor) {
			assert(cursor->output->impl->set_cursor);
			cursor->output->impl->set_cursor(cursor->output, NULL, 1,
				WL_OUTPUT_TRANSFORM_NORMAL, 0, 0, true);
		}
	}
}

bool wlr_output_cursor_move(struct wlr_output_cursor *cursor,
		double x, double y) {
	if (cursor->x == x && cursor->y == y) {
		return true;
	}

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}

	bool was_visible = cursor->visible;
	x *= cursor->output->scale;
	y *= cursor->output->scale;
	cursor->x = x;
	cursor->y = y;
	output_cursor_update_visible(cursor);

	if (!was_visible && !cursor->visible) {
		// Cursor is still hidden, do nothing
		return true;
	}

	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
		return true;
	}

	assert(cursor->output->impl->move_cursor);
	return cursor->output->impl->move_cursor(cursor->output, (int)x, (int)y);
}

struct wlr_output_cursor *wlr_output_cursor_create(struct wlr_output *output) {
	struct wlr_output_cursor *cursor =
		calloc(1, sizeof(struct wlr_output_cursor));
	if (cursor == NULL) {
		return NULL;
	}
	cursor->output = output;
	wl_signal_init(&cursor->events.destroy);
	wl_list_init(&cursor->surface_commit.link);
	cursor->surface_commit.notify = output_cursor_handle_commit;
	wl_list_init(&cursor->surface_destroy.link);
	cursor->surface_destroy.notify = output_cursor_handle_destroy;
	wl_list_insert(&output->cursors, &cursor->link);
	cursor->visible = true; // default position is at (0, 0)
	return cursor;
}

void wlr_output_cursor_destroy(struct wlr_output_cursor *cursor) {
	if (cursor == NULL) {
		return;
	}
	output_cursor_reset(cursor);
	wlr_signal_emit_safe(&cursor->events.destroy, cursor);
	if (cursor->output->hardware_cursor == cursor) {
		// If this cursor was the hardware cursor, disable it
		if (cursor->output->impl->set_cursor) {
			cursor->output->impl->set_cursor(cursor->output, NULL, 1,
				WL_OUTPUT_TRANSFORM_NORMAL, 0, 0, true);
		}
		cursor->output->hardware_cursor = NULL;
	}
	wlr_texture_destroy(cursor->texture);
	wl_list_remove(&cursor->link);
	free(cursor);
}


enum wl_output_transform wlr_output_transform_invert(
		enum wl_output_transform tr) {
	if ((tr & WL_OUTPUT_TRANSFORM_90) && !(tr & WL_OUTPUT_TRANSFORM_FLIPPED)) {
		tr ^= WL_OUTPUT_TRANSFORM_180;
	}
	return tr;
}

enum wl_output_transform wlr_output_transform_compose(
		enum wl_output_transform tr_a, enum wl_output_transform tr_b) {
	uint32_t flipped = (tr_a ^ tr_b) & WL_OUTPUT_TRANSFORM_FLIPPED;
	uint32_t rotated =
		(tr_a + tr_b) & (WL_OUTPUT_TRANSFORM_90 | WL_OUTPUT_TRANSFORM_180);
	return flipped | rotated;
}
