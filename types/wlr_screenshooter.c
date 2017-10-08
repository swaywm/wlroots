#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/render.h>
#include <wlr/types/wlr_screenshooter.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "screenshooter-protocol.h"

static void copy_yflip(uint8_t *dst, uint8_t *src, int32_t height,
		int32_t stride) {
	uint8_t *end = dst + height * stride;
	while (dst < end) {
		memcpy(dst, src, stride);
		dst += stride;
		src -= stride;
	}
}

struct screenshot_state {
	int32_t width, height, stride;
	uint8_t *pixels;
	struct wl_shm_buffer *shm_buffer;
	struct wlr_screenshot *screenshot;
	struct wl_listener frame_listener;
};

static void output_frame_notify(struct wl_listener *listener, void *_data) {
	struct screenshot_state *state = wl_container_of(listener, state,
		frame_listener);
	struct wlr_renderer *renderer = state->screenshot->screenshooter->renderer;
	struct wlr_output *output = state->screenshot->output;

	wlr_output_make_current(output);
	wlr_renderer_read_pixels(renderer, 0, 0, output->width, output->height,
		state->pixels);

	void *data = wl_shm_buffer_get_data(state->shm_buffer);
	wl_shm_buffer_begin_access(state->shm_buffer);
	copy_yflip(data, state->pixels + state->stride * (state->height - 1),
		state->height, state->stride);
	wl_shm_buffer_end_access(state->shm_buffer);

	free(state->pixels);
	wl_list_remove(&listener->link);

	orbital_screenshot_send_done(state->screenshot->resource);

	free(state);
}

static void screenshooter_shoot(struct wl_client *client,
		struct wl_resource *_screenshooter, uint32_t id,
		struct wl_resource *_output, struct wl_resource *_buffer) {
	struct wlr_screenshooter *screenshooter =
		wl_resource_get_user_data(_screenshooter);
	struct wlr_output *output = wl_resource_get_user_data(_output);
	if (!wl_shm_buffer_get(_buffer)) {
		wlr_log(L_ERROR, "Invalid buffer: not a shared memory buffer");
		return;
	}
	struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(_buffer);
	int32_t width = wl_shm_buffer_get_width(shm_buffer);
	int32_t height = wl_shm_buffer_get_height(shm_buffer);
	int32_t stride = wl_shm_buffer_get_stride(shm_buffer);
	if (width < output->width || height < output->height) {
		wlr_log(L_ERROR, "Invalid buffer: too small");
		return;
	}

	uint32_t format = wl_shm_buffer_get_format(shm_buffer);
	if (format != WL_SHM_FORMAT_XRGB8888) {
		wlr_log(L_ERROR, "Invalid buffer: unsupported format");
		return;
	}

	uint8_t *pixels = malloc(stride * height);
	if (pixels == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wlr_screenshot *screenshot =
		calloc(1, sizeof(struct wlr_screenshot));
	if (!screenshot) {
		wl_client_post_no_memory(client);
		return;
	}
	screenshot->output_resource = _output;
	screenshot->output = output;
	screenshot->screenshooter = screenshooter;
	screenshot->resource = wl_resource_create(client,
		&orbital_screenshot_interface, wl_resource_get_version(_screenshooter),
		id);
	wlr_log(L_DEBUG, "new screenshot %p (res %p)", screenshot,
		screenshot->resource);
	wl_resource_set_implementation(screenshot->resource, NULL, screenshot,
		NULL);

	struct screenshot_state *state = calloc(1, sizeof(struct screenshot_state));
	if (!state) {
		wl_client_post_no_memory(client);
		return;
	}
	state->width = width;
	state->height = height;
	state->stride = stride;
	state->pixels = pixels;
	state->shm_buffer = shm_buffer;
	state->screenshot = screenshot;
	state->frame_listener.notify = output_frame_notify;
	wl_signal_add(&output->events.swap_buffers, &state->frame_listener);
}

static struct orbital_screenshooter_interface screenshooter_impl = {
	.shoot = screenshooter_shoot,
};

static void screenshooter_bind(struct wl_client *wl_client,
		void *_screenshooter, uint32_t version, uint32_t id) {
	struct wlr_screenshooter *screenshooter = _screenshooter;
	assert(wl_client && screenshooter);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported screenshooter version,"
			"disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&orbital_screenshooter_interface, version, id);
	wl_resource_set_implementation(wl_resource, &screenshooter_impl,
		screenshooter, NULL);
}

struct wlr_screenshooter *wlr_screenshooter_create(struct wl_display *display,
		struct wlr_renderer *renderer) {
	struct wlr_screenshooter *screenshooter =
		calloc(1, sizeof(struct wlr_screenshooter));
	if (!screenshooter) {
		return NULL;
	}
	screenshooter->renderer = renderer;

	struct wl_global *wl_global = wl_global_create(display,
		&orbital_screenshooter_interface, 1, screenshooter, screenshooter_bind);
	if (!wl_global) {
		free(screenshooter);
		return NULL;
	}
	screenshooter->wl_global = wl_global;

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
