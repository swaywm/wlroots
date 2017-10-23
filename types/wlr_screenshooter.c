#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/render.h>
#include <wlr/types/wlr_screenshooter.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "render/render.h"
#include "screenshooter-protocol.h"

struct screenshot_state {
	struct wl_shm_buffer *shm_buffer;
	struct wlr_screenshot *screenshot;
	struct wl_listener frame_listener;
};

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct screenshot_state *state = wl_container_of(listener, state,
		frame_listener);
	struct wlr_output *output = state->screenshot->output;
	struct wlr_render *rend = wlr_backend_get_render(output->backend);
	struct wl_shm_buffer *shm = state->shm_buffer;

	wlr_output_make_current(output);

	wl_shm_buffer_begin_access(shm);
	wlr_render_read_pixels(rend, wl_shm_buffer_get_format(shm), wl_shm_buffer_get_stride(shm),
		wl_shm_buffer_get_width(shm), wl_shm_buffer_get_height(shm), 0, 0, 0, 0,
		wl_shm_buffer_get_data(shm));
	wl_shm_buffer_end_access(state->shm_buffer);

	wl_list_remove(&listener->link);

	orbital_screenshot_send_done(state->screenshot->resource);

	free(state);
}

static void screenshooter_shoot(struct wl_client *client,
		struct wl_resource *screenshooter_resource, uint32_t id,
		struct wl_resource *output_resource,
		struct wl_resource *buffer_resource) {
	struct wlr_screenshooter *screenshooter =
		wl_resource_get_user_data(screenshooter_resource);
	struct wlr_output *output = wl_resource_get_user_data(output_resource);
	if (!wl_shm_buffer_get(buffer_resource)) {
		wlr_log(L_ERROR, "Invalid buffer: not a shared memory buffer");
		return;
	}
	struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(buffer_resource);
	int32_t width = wl_shm_buffer_get_width(shm_buffer);
	int32_t height = wl_shm_buffer_get_height(shm_buffer);
	if (width < output->width || height < output->height) {
		wlr_log(L_ERROR, "Invalid buffer: too small");
		return;
	}

	uint32_t format = wl_shm_buffer_get_format(shm_buffer);
	if (!wlr_render_format_supported(format)) {
		wlr_log(L_ERROR, "Invalid buffer: unsupported format");
		return;
	}

	struct wlr_screenshot *screenshot = calloc(1, sizeof(*screenshot));
	if (!screenshot) {
		wl_resource_post_no_memory(screenshooter_resource);
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
		NULL);

	wlr_log(L_DEBUG, "new screenshot %p (res %p)", screenshot,
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
	state->frame_listener.notify = output_frame_notify;
	wl_signal_add(&output->events.swap_buffers, &state->frame_listener);
}

static struct orbital_screenshooter_interface screenshooter_impl = {
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

struct wlr_screenshooter *wlr_screenshooter_create(struct wl_display *display) {
	struct wlr_screenshooter *screenshooter = calloc(1, sizeof(*screenshooter));
	if (!screenshooter) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	screenshooter->wl_global = wl_global_create(display,
		&orbital_screenshooter_interface, 1, screenshooter, screenshooter_bind);
	if (screenshooter->wl_global == NULL) {
		free(screenshooter);
		return NULL;
	}

	return screenshooter;
}

void wlr_screenshooter_destroy(struct wlr_screenshooter *screenshooter) {
	if (!screenshooter) {
		return;
	}
	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(screenshooter->wl_global);
	free(screenshooter);
}
