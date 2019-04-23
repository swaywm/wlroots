#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_screenshooter.h>
#include <wlr/util/log.h>
#include "screenshooter-protocol.h"
#include "util/signal.h"

static struct wlr_screenshot *screenshot_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &orbital_screenshot_interface,
		NULL));
	return wl_resource_get_user_data(resource);
}

struct screenshot_state {
	struct wl_shm_buffer *shm_buffer;
	struct wlr_screenshot *screenshot;
	struct wl_listener frame_listener;
};

static void screenshot_destroy(struct wlr_screenshot *screenshot) {
	wl_list_remove(&screenshot->link);
	wl_resource_set_user_data(screenshot->resource, NULL);
	free(screenshot);
}

static void handle_screenshot_resource_destroy(
		struct wl_resource *screenshot_resource) {
	struct wlr_screenshot *screenshot =
		screenshot_from_resource(screenshot_resource);
	if (screenshot != NULL) {
		screenshot_destroy(screenshot);
	}
}

static void output_handle_frame(struct wl_listener *listener, void *_data) {
	struct screenshot_state *state = wl_container_of(listener, state,
		frame_listener);
	struct wlr_output *output = state->screenshot->output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	struct wl_shm_buffer *shm_buffer = state->shm_buffer;

	if (!(output->pending.committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}

	enum wl_shm_format format = wl_shm_buffer_get_format(shm_buffer);
	int32_t width = wl_shm_buffer_get_width(shm_buffer);
	int32_t height = wl_shm_buffer_get_height(shm_buffer);
	int32_t stride = wl_shm_buffer_get_stride(shm_buffer);
	wl_shm_buffer_begin_access(shm_buffer);
	void *data = wl_shm_buffer_get_data(shm_buffer);
	bool ok = wlr_renderer_read_pixels(renderer, format, NULL, stride,
		width, height, 0, 0, 0, 0, data);
	wl_shm_buffer_end_access(shm_buffer);

	if (!ok) {
		wlr_log(WLR_ERROR, "Cannot read pixels");
		goto cleanup;
	}

	orbital_screenshot_send_done(state->screenshot->resource);

cleanup:
	wl_list_remove(&listener->link);
	free(state);
}

static const struct orbital_screenshooter_interface screenshooter_impl;

static struct wlr_screenshooter *screenshooter_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &orbital_screenshooter_interface,
		&screenshooter_impl));
	return wl_resource_get_user_data(resource);
}

static void screenshooter_shoot(struct wl_client *client,
		struct wl_resource *screenshooter_resource, uint32_t id,
		struct wl_resource *output_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_screenshooter *screenshooter =
		screenshooter_from_resource(screenshooter_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	if (renderer == NULL) {
		wlr_log(WLR_ERROR, "Backend doesn't have a renderer");
		return;
	}

	struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(buffer_resource);
	if (shm_buffer == NULL) {
		wlr_log(WLR_ERROR, "Invalid buffer: not a shared memory buffer");
		return;
	}

	int32_t width = wl_shm_buffer_get_width(shm_buffer);
	int32_t height = wl_shm_buffer_get_height(shm_buffer);
	if (width < output->width || height < output->height) {
		wlr_log(WLR_ERROR, "Invalid buffer: too small");
		return;
	}

	uint32_t format = wl_shm_buffer_get_format(shm_buffer);
	if (!wlr_renderer_format_supported(renderer, format)) {
		wlr_log(WLR_ERROR, "Invalid buffer: unsupported format");
		return;
	}

	struct wlr_screenshot *screenshot =
		calloc(1, sizeof(struct wlr_screenshot));
	if (!screenshot) {
		wl_resource_post_no_memory(screenshooter_resource);
		return;
	}
	screenshot->output_resource = output_resource;
	screenshot->output = output;
	screenshot->screenshooter = screenshooter;
	screenshot->resource = wl_resource_create(client,
		&orbital_screenshot_interface,
		wl_resource_get_version(screenshooter_resource), id);
	if (screenshot->resource == NULL) {
		free(screenshot);
		wl_resource_post_no_memory(screenshooter_resource);
		return;
	}
	wl_resource_set_implementation(screenshot->resource, NULL, screenshot,
		handle_screenshot_resource_destroy);
	wl_list_insert(&screenshooter->screenshots, &screenshot->link);

	wlr_log(WLR_DEBUG, "new screenshot %p (res %p)", screenshot,
		screenshot->resource);

	struct screenshot_state *state = calloc(1, sizeof(struct screenshot_state));
	if (!state) {
		wl_resource_destroy(screenshot->resource);
		free(screenshot);
		wl_resource_post_no_memory(screenshooter_resource);
		return;
	}
	state->shm_buffer = shm_buffer;
	state->screenshot = screenshot;
	state->frame_listener.notify = output_handle_frame;
	wl_signal_add(&output->events.precommit, &state->frame_listener);

	// Schedule a buffer commit
	output->needs_frame = true;
	wlr_output_schedule_frame(output);
}

static const struct orbital_screenshooter_interface screenshooter_impl = {
	.shoot = screenshooter_shoot,
};

static void screenshooter_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_screenshooter *screenshooter = data;
	assert(wl_client && screenshooter);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&orbital_screenshooter_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource, &screenshooter_impl,
		screenshooter, NULL);
}

void wlr_screenshooter_destroy(struct wlr_screenshooter *screenshooter) {
	if (!screenshooter) {
		return;
	}
	wl_list_remove(&screenshooter->display_destroy.link);
	struct wlr_screenshot *screenshot, *tmp;
	wl_list_for_each_safe(screenshot, tmp, &screenshooter->screenshots, link) {
		screenshot_destroy(screenshot);
	}
	wlr_signal_emit_safe(&screenshooter->events.destroy, screenshooter);
	wl_global_destroy(screenshooter->global);
	free(screenshooter);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_screenshooter *screenshooter =
		wl_container_of(listener, screenshooter, display_destroy);
	wlr_screenshooter_destroy(screenshooter);
}

struct wlr_screenshooter *wlr_screenshooter_create(struct wl_display *display) {
	struct wlr_screenshooter *screenshooter =
		calloc(1, sizeof(struct wlr_screenshooter));
	if (!screenshooter) {
		return NULL;
	}

	wl_list_init(&screenshooter->screenshots);
	wl_signal_init(&screenshooter->events.destroy);

	screenshooter->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &screenshooter->display_destroy);

	screenshooter->global = wl_global_create(display,
		&orbital_screenshooter_interface, 1, screenshooter, screenshooter_bind);
	if (screenshooter->global == NULL) {
		free(screenshooter);
		return NULL;
	}

	return screenshooter;
}
