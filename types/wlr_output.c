#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <wayland-server-core.h>
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

static void send_geometry(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	wl_output_send_geometry(resource, 0, 0,
		output->phys_width, output->phys_height, output->subpixel,
		output->make, output->model, output->transform);
}

static void send_all_modes(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);

	struct wlr_output_mode *mode;
	wl_list_for_each(mode, &output->modes, link) {
		uint32_t flags = 0;
		if (mode->preferred) {
			flags |= WL_OUTPUT_MODE_PREFERRED;
		}
		if (output->current_mode == mode) {
			flags |= WL_OUTPUT_MODE_CURRENT;
		}
		wl_output_send_mode(resource, flags, mode->width, mode->height,
			mode->refresh);
	}

	if (wl_list_empty(&output->modes)) {
		// Output has no mode, send the current width/height
		wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT,
			output->width, output->height, output->refresh);
	}
}

static void send_current_mode(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	if (output->current_mode != NULL) {
		struct wlr_output_mode *mode = output->current_mode;
		uint32_t flags = WL_OUTPUT_MODE_CURRENT;
		if (mode->preferred) {
			flags |= WL_OUTPUT_MODE_PREFERRED;
		}
		wl_output_send_mode(resource, flags, mode->width, mode->height,
			mode->refresh);
	} else {
		// Output has no mode
		wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, output->width,
			output->height, output->refresh);
	}
}

static void send_scale(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	uint32_t version = wl_resource_get_version(resource);
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
		wl_output_send_scale(resource, (uint32_t)ceil(output->scale));
	}
}

static void send_done(struct wl_resource *resource) {
	uint32_t version = wl_resource_get_version(resource);
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

	send_geometry(resource);
	send_all_modes(resource);
	send_scale(resource);
	send_done(resource);
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
		send_current_mode(resource);
	}
	wlr_output_schedule_done(output);

	wlr_signal_emit_safe(&output->events.mode, output);
}

void wlr_output_set_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	if (output->transform == transform) {
		return;
	}

	output->transform = transform;
	output_update_matrix(output);

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		send_geometry(resource);
	}
	wlr_output_schedule_done(output);

	wlr_signal_emit_safe(&output->events.transform, output);
}

void wlr_output_set_scale(struct wlr_output *output, float scale) {
	if (output->scale == scale) {
		return;
	}

	output->scale = scale;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		send_scale(resource);
	}
	wlr_output_schedule_done(output);

	wlr_signal_emit_safe(&output->events.scale, output);
}

void wlr_output_set_subpixel(struct wlr_output *output,
		enum wl_output_subpixel subpixel) {
	if (output->subpixel == subpixel) {
		return;
	}

	output->subpixel = subpixel;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		send_geometry(resource);
	}
	wlr_output_schedule_done(output);
}

static void schedule_done_handle_idle_timer(void *data) {
	struct wlr_output *output = data;
	output->idle_done = NULL;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		uint32_t version = wl_resource_get_version(resource);
		if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
			wl_output_send_done(resource);
		}
	}
}

void wlr_output_schedule_done(struct wlr_output *output) {
	if (output->idle_done != NULL) {
		return; // Already scheduled
	}

	struct wl_event_loop *ev = wl_display_get_event_loop(output->display);
	output->idle_done =
		wl_event_loop_add_idle(ev, schedule_done_handle_idle_timer, output);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output *output =
		wl_container_of(listener, output, display_destroy);
	wlr_output_destroy_global(output);
}

void wlr_output_init(struct wlr_output *output, struct wlr_backend *backend,
		const struct wlr_output_impl *impl, struct wl_display *display) {
	assert(impl->attach_render && impl->commit);
	if (impl->set_cursor || impl->move_cursor) {
		assert(impl->set_cursor && impl->move_cursor);
	}
	output->backend = backend;
	output->impl = impl;
	output->display = display;
	wl_list_init(&output->modes);
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	output->scale = 1;
	wl_list_init(&output->resources);
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.needs_frame);
	wl_signal_init(&output->events.precommit);
	wl_signal_init(&output->events.commit);
	wl_signal_init(&output->events.present);
	wl_signal_init(&output->events.enable);
	wl_signal_init(&output->events.mode);
	wl_signal_init(&output->events.scale);
	wl_signal_init(&output->events.transform);
	wl_signal_init(&output->events.destroy);
	pixman_region32_init(&output->damage);
	pixman_region32_init(&output->pending.damage);

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

	if (output->idle_frame != NULL) {
		wl_event_source_remove(output->idle_frame);
	}

	if (output->idle_done != NULL) {
		wl_event_source_remove(output->idle_done);
	}

	pixman_region32_fini(&output->pending.damage);
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

struct wlr_output_mode *wlr_output_preferred_mode(struct wlr_output *output) {
	if (wl_list_empty(&output->modes)) {
		return NULL;
	}

	struct wlr_output_mode *mode;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->preferred) {
			return mode;
		}
	}

	// No preferred mode, choose the last one
	return mode;
}

static void output_state_clear_buffer(struct wlr_output_state *state) {
	if (!(state->committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}

	wlr_buffer_unref(state->buffer);
	state->buffer = NULL;

	state->committed &= ~WLR_OUTPUT_STATE_BUFFER;
}

bool wlr_output_attach_render(struct wlr_output *output, int *buffer_age) {
	if (!output->impl->attach_render(output, buffer_age)) {
		return false;
	}

	output_state_clear_buffer(&output->pending);
	output->pending.committed |= WLR_OUTPUT_STATE_BUFFER;
	output->pending.buffer_type = WLR_OUTPUT_STATE_BUFFER_RENDER;
	return true;
}

bool wlr_output_preferred_read_format(struct wlr_output *output,
		enum wl_shm_format *fmt) {
	if (!output->impl->attach_render(output, NULL)) {
		return false;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return false;
	}
	*fmt = renderer->impl->preferred_read_format(renderer);
	return true;
}

void wlr_output_set_damage(struct wlr_output *output,
		pixman_region32_t *damage) {
	pixman_region32_intersect_rect(&output->pending.damage, damage,
		0, 0, output->width, output->height);
	output->pending.committed |= WLR_OUTPUT_STATE_DAMAGE;
}

static void output_state_clear(struct wlr_output_state *state) {
	output_state_clear_buffer(state);
	pixman_region32_clear(&state->damage);
	state->committed = 0;
}

bool wlr_output_commit(struct wlr_output *output) {
	if (output->frame_pending) {
		wlr_log(WLR_ERROR, "Tried to commit when a frame is pending");
		return false;
	}
	if (output->idle_frame != NULL) {
		wl_event_source_remove(output->idle_frame);
		output->idle_frame = NULL;
	}

	if (!(output->pending.committed & WLR_OUTPUT_STATE_BUFFER)) {
		wlr_log(WLR_ERROR, "Tried to commit without attaching a buffer");
		return false;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct wlr_output_event_precommit event = {
		.output = output,
		.when = &now,
	};
	wlr_signal_emit_safe(&output->events.precommit, &event);

	if (!output->impl->commit(output)) {
		output_state_clear(&output->pending);
		return false;
	}

	wlr_signal_emit_safe(&output->events.commit, output);

	output->frame_pending = true;
	output->needs_frame = false;
	output_state_clear(&output->pending);
	pixman_region32_clear(&output->damage);
	return true;
}

bool wlr_output_attach_buffer(struct wlr_output *output,
		struct wlr_buffer *buffer) {
	if (!output->impl->attach_buffer) {
		return false;
	}
	if (output->attach_render_locks > 0) {
		return false;
	}

	if (!output->impl->attach_buffer(output, buffer)) {
		return false;
	}

	output_state_clear_buffer(&output->pending);
	output->pending.committed |= WLR_OUTPUT_STATE_BUFFER;
	output->pending.buffer_type = WLR_OUTPUT_STATE_BUFFER_SCANOUT;
	output->pending.buffer = wlr_buffer_ref(buffer);
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

void wlr_output_update_needs_frame(struct wlr_output *output) {
	output->needs_frame = true;
	wlr_signal_emit_safe(&output->events.needs_frame, output);
}

void wlr_output_damage_whole(struct wlr_output *output) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	pixman_region32_union_rect(&output->damage, &output->damage, 0, 0,
		width, height);
	wlr_output_update_needs_frame(output);
}

struct wlr_output *wlr_output_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_output_interface,
		&output_impl));
	return wl_resource_get_user_data(resource);
}

void wlr_output_lock_attach_render(struct wlr_output *output, bool lock) {
	if (lock) {
		++output->attach_render_locks;
	} else {
		assert(output->attach_render_locks > 0);
		--output->attach_render_locks;
	}
	wlr_log(WLR_DEBUG, "%s direct scan-out on output '%s' (locks: %d)",
		lock ? "Disabling" : "Enabling", output->name,
		output->attach_render_locks);
}

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
}

#if 0
bool wlr_output_cursor_set_image(struct wlr_output_cursor *cursor,
		const uint8_t *pixels, int32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(cursor->output->backend);
	if (!renderer) {
		// if the backend has no renderer, we can't draw a cursor, but this is
		// actually okay, for ex. with the noop backend
		return true;
	}

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
#endif

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
