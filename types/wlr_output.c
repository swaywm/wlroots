#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "backend/backend.h"
#include "render/allocator.h"
#include "render/drm_format_set.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "util/global.h"
#include "util/signal.h"

#define OUTPUT_VERSION 3

static void send_geometry(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	wl_output_send_geometry(resource, 0, 0,
		output->phys_width, output->phys_height, output->subpixel,
		output->make, output->model, output->transform);
}

static void send_current_mode(struct wl_resource *resource) {
	struct wlr_output *output = wlr_output_from_resource(resource);
	if (output->current_mode != NULL) {
		struct wlr_output_mode *mode = output->current_mode;
		wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT,
			mode->width, mode->height, mode->refresh);
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
	// `output` can be NULL if the output global is being destroyed
	struct wlr_output *output = data;

	struct wl_resource *resource = wl_resource_create(wl_client,
		&wl_output_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(resource, &output_impl, output,
		output_handle_resource_destroy);

	if (output == NULL) {
		wl_list_init(wl_resource_get_link(resource));
		return;
	}

	wl_list_insert(&output->resources, wl_resource_get_link(resource));

	send_geometry(resource);
	send_current_mode(resource);
	send_scale(resource);
	send_done(resource);

	struct wlr_output_event_bind evt = {
		.output = output,
		.resource = resource,
	};

	wlr_signal_emit_safe(&output->events.bind, &evt);
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

	wlr_global_destroy_safe(output->global, output->display);
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
	wlr_matrix_identity(output->transform_matrix);
	if (output->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
		int tr_width, tr_height;
		wlr_output_transformed_resolution(output, &tr_width, &tr_height);

		wlr_matrix_translate(output->transform_matrix,
			output->width / 2.0, output->height / 2.0);
		wlr_matrix_transform(output->transform_matrix, output->transform);
		wlr_matrix_translate(output->transform_matrix,
			- tr_width / 2.0, - tr_height / 2.0);
	}
}

void wlr_output_enable(struct wlr_output *output, bool enable) {
	if (output->enabled == enable) {
		output->pending.committed &= ~WLR_OUTPUT_STATE_ENABLED;
		return;
	}

	output->pending.committed |= WLR_OUTPUT_STATE_ENABLED;
	output->pending.enabled = enable;
}

static void output_state_clear_mode(struct wlr_output_state *state) {
	if (!(state->committed & WLR_OUTPUT_STATE_MODE)) {
		return;
	}

	state->mode = NULL;

	state->committed &= ~WLR_OUTPUT_STATE_MODE;
}

void wlr_output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	output_state_clear_mode(&output->pending);

	if (output->current_mode == mode) {
		return;
	}

	output->pending.committed |= WLR_OUTPUT_STATE_MODE;
	output->pending.mode_type = WLR_OUTPUT_STATE_MODE_FIXED;
	output->pending.mode = mode;
}

void wlr_output_set_custom_mode(struct wlr_output *output, int32_t width,
		int32_t height, int32_t refresh) {
	output_state_clear_mode(&output->pending);

	if (output->width == width && output->height == height &&
			output->refresh == refresh) {
		return;
	}

	output->pending.committed |= WLR_OUTPUT_STATE_MODE;
	output->pending.mode_type = WLR_OUTPUT_STATE_MODE_CUSTOM;
	output->pending.custom_mode.width = width;
	output->pending.custom_mode.height = height;
	output->pending.custom_mode.refresh = refresh;
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

	if (output->swapchain != NULL &&
			(output->swapchain->width != output->width ||
			output->swapchain->height != output->height)) {
		wlr_swapchain_destroy(output->swapchain);
		output->swapchain = NULL;
	}

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
		output->pending.committed &= ~WLR_OUTPUT_STATE_TRANSFORM;
		return;
	}

	output->pending.committed |= WLR_OUTPUT_STATE_TRANSFORM;
	output->pending.transform = transform;
}

void wlr_output_set_scale(struct wlr_output *output, float scale) {
	if (output->scale == scale) {
		output->pending.committed &= ~WLR_OUTPUT_STATE_SCALE;
		return;
	}

	output->pending.committed |= WLR_OUTPUT_STATE_SCALE;
	output->pending.scale = scale;
}

void wlr_output_enable_adaptive_sync(struct wlr_output *output, bool enabled) {
	bool currently_enabled =
		output->adaptive_sync_status != WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED;
	if (currently_enabled == enabled) {
		output->pending.committed &= ~WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
		return;
	}

	output->pending.committed |= WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
	output->pending.adaptive_sync_enabled = enabled;
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

void wlr_output_set_description(struct wlr_output *output, const char *desc) {
	if (output->description != NULL && desc != NULL &&
			strcmp(output->description, desc) == 0) {
		return;
	}

	free(output->description);
	if (desc != NULL) {
		output->description = strdup(desc);
	} else {
		output->description = NULL;
	}

	wlr_signal_emit_safe(&output->events.description, output);
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
	assert(impl->commit);
	if (impl->set_cursor || impl->move_cursor) {
		assert(impl->set_cursor && impl->move_cursor);
	}
	output->backend = backend;
	output->impl = impl;
	output->display = display;
	wl_list_init(&output->modes);
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	output->scale = 1;
	output->commit_seq = 0;
	wl_list_init(&output->cursors);
	wl_list_init(&output->resources);
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.damage);
	wl_signal_init(&output->events.needs_frame);
	wl_signal_init(&output->events.precommit);
	wl_signal_init(&output->events.commit);
	wl_signal_init(&output->events.present);
	wl_signal_init(&output->events.bind);
	wl_signal_init(&output->events.enable);
	wl_signal_init(&output->events.mode);
	wl_signal_init(&output->events.description);
	wl_signal_init(&output->events.destroy);
	pixman_region32_init(&output->pending.damage);

	const char *no_hardware_cursors = getenv("WLR_NO_HARDWARE_CURSORS");
	if (no_hardware_cursors != NULL && strcmp(no_hardware_cursors, "1") == 0) {
		wlr_log(WLR_DEBUG,
			"WLR_NO_HARDWARE_CURSORS set, forcing software cursors");
		output->software_cursor_locks = 1;
	}

	output->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &output->display_destroy);
}

static void output_clear_back_buffer(struct wlr_output *output);

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) {
		return;
	}

	wlr_buffer_unlock(output->front_buffer);
	output->front_buffer = NULL;

	wl_list_remove(&output->display_destroy.link);
	wlr_output_destroy_global(output);
	output_clear_back_buffer(output);

	wlr_signal_emit_safe(&output->events.destroy, output);

	// The backend is responsible for free-ing the list of modes

	struct wlr_output_cursor *cursor, *tmp_cursor;
	wl_list_for_each_safe(cursor, tmp_cursor, &output->cursors, link) {
		wlr_output_cursor_destroy(cursor);
	}

	wlr_swapchain_destroy(output->cursor_swapchain);
	wlr_buffer_unlock(output->cursor_front_buffer);

	wlr_swapchain_destroy(output->swapchain);

	if (output->idle_frame != NULL) {
		wl_event_source_remove(output->idle_frame);
	}

	if (output->idle_done != NULL) {
		wl_event_source_remove(output->idle_done);
	}

	free(output->description);

	pixman_region32_fini(&output->pending.damage);

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

	// No preferred mode, choose the first one
	return wl_container_of(output->modes.next, mode, link);
}

static void output_state_clear_buffer(struct wlr_output_state *state) {
	if (!(state->committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}

	wlr_buffer_unlock(state->buffer);
	state->buffer = NULL;

	state->committed &= ~WLR_OUTPUT_STATE_BUFFER;
}

static struct wlr_drm_format *output_pick_format(struct wlr_output *output,
		const struct wlr_drm_format_set *display_formats);
static void output_pending_resolution(struct wlr_output *output, int *width,
		int *height);

/**
 * Ensure the output has a suitable swapchain. The swapchain is re-created if
 * necessary.
 *
 * If allow_modifiers is set to true, the swapchain's format may use modifiers.
 * If set to false, the swapchain's format is guaranteed to not use modifiers.
 */
static bool output_create_swapchain(struct wlr_output *output,
		bool allow_modifiers) {
	int width, height;
	output_pending_resolution(output, &width, &height);

	if (output->swapchain != NULL && output->swapchain->width == width &&
			output->swapchain->height == height &&
			(allow_modifiers || output->swapchain->format->len == 0)) {
		return true;
	}

	struct wlr_allocator *allocator = backend_get_allocator(output->backend);
	if (allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to get backend allocator");
		return false;
	}

	const struct wlr_drm_format_set *display_formats = NULL;
	if (output->impl->get_primary_formats) {
		display_formats =
			output->impl->get_primary_formats(output, allocator->buffer_caps);
		if (display_formats == NULL) {
			wlr_log(WLR_ERROR, "Failed to get primary display formats");
			return false;
		}
	}

	struct wlr_drm_format *format = output_pick_format(output, display_formats);
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to pick primary buffer format for output '%s'",
			output->name);
		return false;
	}
	wlr_log(WLR_DEBUG, "Choosing primary buffer format 0x%"PRIX32" for output '%s'",
		format->format, output->name);

	if (!allow_modifiers && (format->len != 1 || format->modifiers[0] != DRM_FORMAT_MOD_LINEAR)) {
		format->len = 0;
	}

	struct wlr_swapchain *swapchain =
		wlr_swapchain_create(allocator, width, height, format);
	free(format);
	if (swapchain == NULL) {
		wlr_log(WLR_ERROR, "Failed to create output swapchain");
		return false;
	}

	wlr_swapchain_destroy(output->swapchain);
	output->swapchain = swapchain;

	return true;
}

static bool output_attach_back_buffer(struct wlr_output *output,
		int *buffer_age) {
	assert(output->back_buffer == NULL);

	if (!output_create_swapchain(output, true)) {
		return false;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer != NULL);

	struct wlr_buffer *buffer =
		wlr_swapchain_acquire(output->swapchain, buffer_age);
	if (buffer == NULL) {
		return false;
	}

	if (!renderer_bind_buffer(renderer, buffer)) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	output->back_buffer = buffer;
	return true;
}

static void output_clear_back_buffer(struct wlr_output *output) {
	if (output->back_buffer == NULL) {
		return;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer != NULL);

	renderer_bind_buffer(renderer, NULL);

	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = NULL;
}

bool wlr_output_attach_render(struct wlr_output *output, int *buffer_age) {
	if (!output_attach_back_buffer(output, buffer_age)) {
		return false;
	}
	wlr_output_attach_buffer(output, output->back_buffer);
	return true;
}

uint32_t wlr_output_preferred_read_format(struct wlr_output *output) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return DRM_FORMAT_INVALID;
	}

	if (!output_attach_back_buffer(output, NULL)) {
		return false;
	}

	uint32_t fmt = renderer->impl->preferred_read_format(renderer);

	output_clear_back_buffer(output);

	return fmt;
}

void wlr_output_set_damage(struct wlr_output *output,
		pixman_region32_t *damage) {
	pixman_region32_intersect_rect(&output->pending.damage, damage,
		0, 0, output->width, output->height);
	output->pending.committed |= WLR_OUTPUT_STATE_DAMAGE;
}

static void output_state_clear_gamma_lut(struct wlr_output_state *state) {
	free(state->gamma_lut);
	state->gamma_lut = NULL;
	state->committed &= ~WLR_OUTPUT_STATE_GAMMA_LUT;
}

static void output_state_clear(struct wlr_output_state *state) {
	output_state_clear_buffer(state);
	output_state_clear_gamma_lut(state);
	pixman_region32_clear(&state->damage);
	state->committed = 0;
}

static void output_pending_resolution(struct wlr_output *output, int *width,
		int *height) {
	if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		switch (output->pending.mode_type) {
		case WLR_OUTPUT_STATE_MODE_FIXED:
			*width = output->pending.mode->width;
			*height = output->pending.mode->height;
			return;
		case WLR_OUTPUT_STATE_MODE_CUSTOM:
			*width = output->pending.custom_mode.width;
			*height = output->pending.custom_mode.height;
			return;
		}
		abort();
	} else {
		*width = output->width;
		*height = output->height;
	}
}

static bool output_attach_empty_buffer(struct wlr_output *output) {
	assert(!(output->pending.committed & WLR_OUTPUT_STATE_BUFFER));

	if (!wlr_output_attach_render(output, NULL)) {
		return false;
	}

	int width, height;
	output_pending_resolution(output, &width, &height);

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	wlr_renderer_begin(renderer, width, height);
	wlr_renderer_clear(renderer, (float[]){0, 0, 0, 0});
	wlr_renderer_end(renderer);

	return true;
}

static bool output_ensure_buffer(struct wlr_output *output) {
	// If we're lighting up an output or changing its mode, make sure to
	// provide a new buffer
	bool needs_new_buffer = false;
	if ((output->pending.committed & WLR_OUTPUT_STATE_ENABLED) &&
			output->pending.enabled) {
		needs_new_buffer = true;
	}
	if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		needs_new_buffer = true;
	}
	if (!needs_new_buffer ||
			(output->pending.committed & WLR_OUTPUT_STATE_BUFFER)) {
		return true;
	}

	wlr_log(WLR_DEBUG, "Attaching empty buffer to output for modeset");

	if (!output_attach_empty_buffer(output)) {
		goto error;
	}
	if (!output->impl->test || output->impl->test(output)) {
		return true;
	}

	output_clear_back_buffer(output);
	output->pending.committed &= ~WLR_OUTPUT_STATE_BUFFER;

	if (output->swapchain->format->len == 0) {
		return false;
	}

	// The test failed for a buffer which has modifiers, try disabling
	// modifiers to see if that makes a difference.
	wlr_log(WLR_DEBUG, "Output modeset test failed, retrying without modifiers");

	if (!output_create_swapchain(output, false)) {
		return false;
	}
	if (!output_attach_empty_buffer(output)) {
		goto error;
	}
	if (!output->impl->test(output)) {
		goto error;
	}
	return true;

error:
	output_clear_back_buffer(output);
	output->pending.committed &= ~WLR_OUTPUT_STATE_BUFFER;
	return false;
}

static bool output_basic_test(struct wlr_output *output) {
	if (output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		if (output->frame_pending) {
			wlr_log(WLR_DEBUG, "Tried to commit a buffer while a frame is pending");
			return false;
		}

		if (output->back_buffer == NULL) {
			if (output->attach_render_locks > 0) {
				wlr_log(WLR_DEBUG, "Direct scan-out disabled by lock");
				return false;
			}

			// If the output has at least one software cursor, refuse to attach the
			// buffer
			struct wlr_output_cursor *cursor;
			wl_list_for_each(cursor, &output->cursors, link) {
				if (cursor->enabled && cursor->visible &&
						cursor != output->hardware_cursor) {
					wlr_log(WLR_DEBUG,
						"Direct scan-out disabled by software cursor");
					return false;
				}
			}

			// If the size doesn't match, reject buffer (scaling is not
			// supported)
			int pending_width, pending_height;
			output_pending_resolution(output, &pending_width, &pending_height);
			if (output->pending.buffer->width != pending_width ||
					output->pending.buffer->height != pending_height) {
				wlr_log(WLR_DEBUG, "Direct scan-out buffer size mismatch");
				return false;
			}
		}
	}

	bool enabled = output->enabled;
	if (output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		enabled = output->pending.enabled;
	}

	if (!enabled && output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		wlr_log(WLR_DEBUG, "Tried to commit a buffer on a disabled output");
		return false;
	}
	if (!enabled && output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		wlr_log(WLR_DEBUG, "Tried to modeset a disabled output");
		return false;
	}
	if (!enabled && output->pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED) {
		wlr_log(WLR_DEBUG, "Tried to enable adaptive sync on a disabled output");
		return false;
	}
	if (!enabled && output->pending.committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		wlr_log(WLR_DEBUG, "Tried to set the gamma lut on a disabled output");
		return false;
	}

	return true;
}

bool wlr_output_test(struct wlr_output *output) {
	if (!output_basic_test(output)) {
		return false;
	}
	if (!output_ensure_buffer(output)) {
		return false;
	}
	if (!output->impl->test) {
		return true;
	}
	return output->impl->test(output);
}

bool wlr_output_commit(struct wlr_output *output) {
	if (!output_basic_test(output)) {
		wlr_log(WLR_ERROR, "Basic output test failed for %s", output->name);
		return false;
	}

	if (!output_ensure_buffer(output)) {
		return false;
	}

	if ((output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
			output->idle_frame != NULL) {
		wl_event_source_remove(output->idle_frame);
		output->idle_frame = NULL;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct wlr_output_event_precommit pre_event = {
		.output = output,
		.when = &now,
	};
	wlr_signal_emit_safe(&output->events.precommit, &pre_event);

	// output_clear_back_buffer detaches the buffer from the renderer. This is
	// important to do before calling impl->commit(), because this marks an
	// implicit rendering synchronization point. The backend needs it to avoid
	// displaying a buffer when asynchronous GPU work isn't finished.
	struct wlr_buffer *back_buffer = NULL;
	if ((output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
			output->back_buffer != NULL) {
		back_buffer = wlr_buffer_lock(output->back_buffer);
		output_clear_back_buffer(output);
	}

	if (!output->impl->commit(output)) {
		wlr_buffer_unlock(back_buffer);
		output_state_clear(&output->pending);
		return false;
	}

	if (output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		struct wlr_output_cursor *cursor;
		wl_list_for_each(cursor, &output->cursors, link) {
			if (!cursor->enabled || !cursor->visible || cursor->surface == NULL) {
				continue;
			}
			wlr_surface_send_frame_done(cursor->surface, &now);
		}
	}

	output->commit_seq++;

	bool scale_updated = output->pending.committed & WLR_OUTPUT_STATE_SCALE;
	if (scale_updated) {
		output->scale = output->pending.scale;
	}

	if (output->pending.committed & WLR_OUTPUT_STATE_TRANSFORM) {
		output->transform = output->pending.transform;
		output_update_matrix(output);
	}

	bool geometry_updated = output->pending.committed &
		(WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_TRANSFORM);
	if (geometry_updated || scale_updated) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &output->resources) {
			if (geometry_updated) {
				send_geometry(resource);
			}
			if (scale_updated) {
				send_scale(resource);
			}
		}
		wlr_output_schedule_done(output);
	}

	// Unset the front-buffer when a new buffer will replace it or when the
	// output is getting disabled
	if ((output->pending.committed & WLR_OUTPUT_STATE_BUFFER) ||
			((output->pending.committed & WLR_OUTPUT_STATE_ENABLED) &&
				!output->pending.enabled)) {
		wlr_buffer_unlock(output->front_buffer);
		output->front_buffer = NULL;
	}

	if (output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		output->frame_pending = true;
		output->needs_frame = false;
	}

	if (back_buffer != NULL) {
		wlr_swapchain_set_buffer_submitted(output->swapchain, back_buffer);
		wlr_buffer_unlock(output->front_buffer);
		output->front_buffer = back_buffer;
	}

	uint32_t committed = output->pending.committed;
	output_state_clear(&output->pending);

	struct wlr_output_event_commit event = {
		.output = output,
		.committed = committed,
		.when = &now,
	};
	wlr_signal_emit_safe(&output->events.commit, &event);

	return true;
}

void wlr_output_rollback(struct wlr_output *output) {
	output_clear_back_buffer(output);
	output_state_clear(&output->pending);
}

void wlr_output_attach_buffer(struct wlr_output *output,
		struct wlr_buffer *buffer) {
	output_state_clear_buffer(&output->pending);
	output->pending.committed |= WLR_OUTPUT_STATE_BUFFER;
	output->pending.buffer = wlr_buffer_lock(buffer);
}

void wlr_output_send_frame(struct wlr_output *output) {
	output->frame_pending = false;
	wlr_signal_emit_safe(&output->events.frame, output);
}

static void schedule_frame_handle_idle_timer(void *data) {
	struct wlr_output *output = data;
	output->idle_frame = NULL;
	if (!output->frame_pending) {
		wlr_output_send_frame(output);
	}
}

void wlr_output_schedule_frame(struct wlr_output *output) {
	// Make sure the compositor commits a new frame. This is necessary to make
	// clients which ask for frame callbacks without submitting a new buffer
	// work.
	wlr_output_update_needs_frame(output);

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
		event->commit_seq = output->commit_seq + 1;
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

void wlr_output_set_gamma(struct wlr_output *output, size_t size,
		const uint16_t *r, const uint16_t *g, const uint16_t *b) {
	output_state_clear_gamma_lut(&output->pending);

	output->pending.gamma_lut_size = size;
	output->pending.gamma_lut = malloc(3 * size * sizeof(uint16_t));
	if (output->pending.gamma_lut == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return;
	}
	memcpy(output->pending.gamma_lut, r, size * sizeof(uint16_t));
	memcpy(output->pending.gamma_lut + size, g, size * sizeof(uint16_t));
	memcpy(output->pending.gamma_lut + 2 * size, b, size * sizeof(uint16_t));

	output->pending.committed |= WLR_OUTPUT_STATE_GAMMA_LUT;
}

size_t wlr_output_get_gamma_size(struct wlr_output *output) {
	if (!output->impl->get_gamma_size) {
		return 0;
	}
	return output->impl->get_gamma_size(output);
}

bool wlr_output_export_dmabuf(struct wlr_output *output,
		struct wlr_dmabuf_attributes *attribs) {
	if (output->front_buffer == NULL) {
		return false;
	}

	struct wlr_dmabuf_attributes buf_attribs = {0};
	if (!wlr_buffer_get_dmabuf(output->front_buffer, &buf_attribs)) {
		return false;
	}
	return wlr_dmabuf_attributes_copy(attribs, &buf_attribs);
}

void wlr_output_update_needs_frame(struct wlr_output *output) {
	if (output->needs_frame) {
		return;
	}
	output->needs_frame = true;
	wlr_signal_emit_safe(&output->events.needs_frame, output);
}

void wlr_output_damage_whole(struct wlr_output *output) {
	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, 0, 0, width, height);

	struct wlr_output_event_damage event = {
		.output = output,
		.damage = &damage,
	};
	wlr_signal_emit_safe(&output->events.damage, &event);

	pixman_region32_fini(&damage);
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
		output->impl->set_cursor(output, NULL, 0, 0);
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

	pixman_region32_t damage;
	pixman_region32_init_rect(&damage, box.x, box.y, box.width, box.height);

	struct wlr_output_event_damage event = {
		.output = cursor->output,
		.damage = &damage,
	};
	wlr_signal_emit_safe(&cursor->output->events.damage, &event);

	pixman_region32_fini(&damage);
}

static void output_cursor_reset(struct wlr_output_cursor *cursor) {
	if (cursor->output->hardware_cursor != cursor) {
		output_cursor_damage_whole(cursor);
	}
	if (cursor->surface != NULL) {
		wl_list_remove(&cursor->surface_commit.link);
		wl_list_remove(&cursor->surface_destroy.link);
		if (cursor->visible) {
			wlr_surface_send_leave(cursor->surface, cursor->output);
		}
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

static struct wlr_drm_format *output_pick_format(struct wlr_output *output,
		const struct wlr_drm_format_set *display_formats) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	struct wlr_allocator *allocator = backend_get_allocator(output->backend);
	assert(renderer != NULL && allocator != NULL);

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_render_formats(renderer);
	if (render_formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get render formats");
		return NULL;
	}

	struct wlr_drm_format *format = NULL;
	const uint32_t candidates[] = { DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888 };
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		uint32_t fmt = candidates[i];

		const struct wlr_drm_format *render_format =
			wlr_drm_format_set_get(render_formats, fmt);
		if (render_format == NULL) {
			wlr_log(WLR_DEBUG, "Renderer doesn't support format 0x%"PRIX32, fmt);
			continue;
		}

		if (display_formats != NULL) {
			const struct wlr_drm_format *display_format =
				wlr_drm_format_set_get(display_formats, fmt);
			if (display_format == NULL) {
				wlr_log(WLR_DEBUG, "Output doesn't support format 0x%"PRIX32, fmt);
				continue;
			}
			format = wlr_drm_format_intersect(display_format, render_format);
		} else {
			// The output can display any format
			format = wlr_drm_format_dup(render_format);
		}

		if (format == NULL) {
			wlr_log(WLR_DEBUG, "Failed to intersect display and render "
				"modifiers for format 0x%"PRIX32, fmt);
		} else {
			break;
		}
	}
	if (format == NULL) {
		wlr_log(WLR_ERROR, "Failed to choose a format for output '%s'",
			output->name);
		return NULL;
	}

	return format;
}

static struct wlr_drm_format *output_pick_cursor_format(struct wlr_output *output) {
	struct wlr_allocator *allocator = backend_get_allocator(output->backend);
	assert(allocator != NULL);

	const struct wlr_drm_format_set *display_formats = NULL;
	if (output->impl->get_cursor_formats) {
		display_formats =
			output->impl->get_cursor_formats(output, allocator->buffer_caps);
		if (display_formats == NULL) {
			wlr_log(WLR_ERROR, "Failed to get cursor display formats");
			return NULL;
		}
	}

	return output_pick_format(output, display_formats);
}

static struct wlr_buffer *render_cursor_buffer(struct wlr_output_cursor *cursor) {
	struct wlr_output *output = cursor->output;

	float scale = output->scale;
	enum wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
	struct wlr_texture *texture = cursor->texture;
	if (cursor->surface != NULL) {
		texture = wlr_surface_get_texture(cursor->surface);
		scale = cursor->surface->current.scale;
		transform = cursor->surface->current.transform;
	}
	if (texture == NULL) {
		return NULL;
	}

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	if (renderer == NULL) {
		wlr_log(WLR_ERROR, "Failed to get backend renderer");
		return NULL;
	}

	struct wlr_allocator *allocator = backend_get_allocator(output->backend);
	if (allocator == NULL) {
		wlr_log(WLR_ERROR, "Failed to get backend allocator");
		return NULL;
	}

	int width = texture->width;
	int height = texture->height;
	if (output->impl->get_cursor_size) {
		// Apply hardware limitations on buffer size
		output->impl->get_cursor_size(cursor->output, &width, &height);
		if ((int)texture->width > width || (int)texture->height > height) {
			wlr_log(WLR_DEBUG, "Cursor texture too large (%dx%d), "
				"exceeds hardware limitations (%dx%d)", texture->width,
				texture->height, width, height);
			return NULL;
		}
	}

	if (output->cursor_swapchain == NULL ||
			output->cursor_swapchain->width != width ||
			output->cursor_swapchain->height != height) {
		struct wlr_drm_format *format =
			output_pick_cursor_format(output);
		if (format == NULL) {
			wlr_log(WLR_ERROR, "Failed to pick cursor format");
			return NULL;
		}

		wlr_swapchain_destroy(output->cursor_swapchain);
		output->cursor_swapchain = wlr_swapchain_create(allocator,
			width, height, format);
		if (output->cursor_swapchain == NULL) {
			wlr_log(WLR_ERROR, "Failed to create cursor swapchain");
			return NULL;
		}
	}

	struct wlr_buffer *buffer =
		wlr_swapchain_acquire(output->cursor_swapchain, NULL);
	if (buffer == NULL) {
		return NULL;
	}

	struct wlr_box cursor_box = {
		.width = texture->width * output->scale / scale,
		.height = texture->height * output->scale / scale,
	};

	float output_matrix[9];
	wlr_matrix_identity(output_matrix);
	if (output->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
		struct wlr_box tr_size = {
			.width = buffer->width,
			.height = buffer->height,
		};
		wlr_box_transform(&tr_size, &tr_size, output->transform, 0, 0);

		wlr_matrix_translate(output_matrix, buffer->width / 2.0,
			buffer->height / 2.0);
		wlr_matrix_transform(output_matrix, output->transform);
		wlr_matrix_translate(output_matrix, - tr_size.width / 2.0,
			- tr_size.height / 2.0);
	}

	float matrix[9];
	wlr_matrix_project_box(matrix, &cursor_box, transform, 0, output_matrix);

	if (!wlr_renderer_begin_with_buffer(renderer, buffer)) {
		wlr_buffer_unlock(buffer);
		return NULL;
	}

	wlr_renderer_clear(renderer, (float[]){ 0.0, 0.0, 0.0, 0.0 });
	wlr_render_texture_with_matrix(renderer, texture, matrix, 1.0);

	wlr_renderer_end(renderer);

	return buffer;
}

static bool output_cursor_attempt_hardware(struct wlr_output_cursor *cursor) {
	struct wlr_output *output = cursor->output;

	if (!output->impl->set_cursor ||
			output->software_cursor_locks > 0) {
		return false;
	}

	struct wlr_output_cursor *hwcur = output->hardware_cursor;
	if (hwcur != NULL && hwcur != cursor) {
		return false;
	}

	struct wlr_texture *texture = cursor->texture;
	if (cursor->surface != NULL) {
		// TODO: try using the surface buffer directly
		texture = wlr_surface_get_texture(cursor->surface);
	}

	// If the cursor was hidden or was a software cursor, the hardware
	// cursor position is outdated
	output->impl->move_cursor(cursor->output,
		(int)cursor->x, (int)cursor->y);

	struct wlr_buffer *buffer = NULL;
	if (texture != NULL) {
		buffer = render_cursor_buffer(cursor);
		if (buffer == NULL) {
			wlr_log(WLR_ERROR, "Failed to render cursor buffer");
			return false;
		}
	}

	struct wlr_box hotspot = {
		.x = cursor->hotspot_x,
		.y = cursor->hotspot_y,
	};
	wlr_box_transform(&hotspot, &hotspot,
		wlr_output_transform_invert(output->transform),
		buffer ? buffer->width : 0, buffer ? buffer->height : 0);

	bool ok = output->impl->set_cursor(cursor->output, buffer,
		hotspot.x, hotspot.y);
	if (ok) {
		wlr_buffer_unlock(output->cursor_front_buffer);
		output->cursor_front_buffer = buffer;
		output->hardware_cursor = cursor;
	} else {
		wlr_buffer_unlock(buffer);
	}
	return ok;
}

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
			DRM_FORMAT_ARGB8888, stride, width, height, pixels);
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
			struct wlr_buffer *buffer = cursor->output->cursor_front_buffer;

			struct wlr_box hotspot = {
				.x = cursor->hotspot_x,
				.y = cursor->hotspot_y,
			};
			wlr_box_transform(&hotspot, &hotspot,
				wlr_output_transform_invert(cursor->output->transform),
				buffer ? buffer->width : 0, buffer ? buffer->height : 0);

			assert(cursor->output->impl->set_cursor);
			cursor->output->impl->set_cursor(cursor->output,
				buffer, hotspot.x, hotspot.y);
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
			cursor->output->impl->set_cursor(cursor->output, NULL, 0, 0);
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
			cursor->output->impl->set_cursor(cursor->output, NULL, 0, 0);
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
	uint32_t rotation_mask = WL_OUTPUT_TRANSFORM_90 | WL_OUTPUT_TRANSFORM_180;
	uint32_t rotated;
	if (tr_b & WL_OUTPUT_TRANSFORM_FLIPPED) {
		// When a rotation of k degrees is followed by a flip, the
		// equivalent transform is a flip followed by a rotation of
		// -k degrees.
		rotated = (tr_b - tr_a) & rotation_mask;
	} else {
		rotated = (tr_a + tr_b) & rotation_mask;
	}
	return flipped | rotated;
}
