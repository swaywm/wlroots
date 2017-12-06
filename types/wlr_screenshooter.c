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

static void screenshot_destroy(struct wlr_screenshot *screenshot) {
	wl_list_remove(&screenshot->link);
	wl_resource_set_user_data(screenshot->resource, NULL);
	free(screenshot);
}

static void handle_screenshot_resource_destroy(
		struct wl_resource *screenshot_resource) {
	struct wlr_screenshot *screenshot =
		wl_resource_get_user_data(screenshot_resource);
	if (screenshot != NULL) {
		screenshot_destroy(screenshot);
	}
}

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
		free(pixels);
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
		free(pixels);
		wl_resource_post_no_memory(screenshooter_resource);
		return;
	}
	wl_resource_set_implementation(screenshot->resource, NULL, screenshot,
		handle_screenshot_resource_destroy);
	wl_list_insert(&screenshooter->screenshots, &screenshot->link);

	wlr_log(L_DEBUG, "new screenshot %p (res %p)", screenshot,
		screenshot->resource);

	struct screenshot_state *state = calloc(1, sizeof(struct screenshot_state));
	if (!state) {
		wl_resource_destroy(screenshot->resource);
		free(screenshot);
		free(pixels);
		wl_resource_post_no_memory(screenshooter_resource);
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
	wl_signal_emit(&screenshooter->events.destroy, screenshooter);
	wl_list_remove(&screenshooter->display_destroy.link);
	struct wlr_screenshot *screenshot, *tmp;
	wl_list_for_each_safe(screenshot, tmp, &screenshooter->screenshots, link) {
		screenshot_destroy(screenshot);
	}
	wl_global_destroy(screenshooter->wl_global);
	free(screenshooter);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_screenshooter *screenshooter =
		wl_container_of(listener, screenshooter, display_destroy);
	wlr_screenshooter_destroy(screenshooter);
}

struct wlr_screenshooter *wlr_screenshooter_create(struct wl_display *display,
		struct wlr_renderer *renderer) {
	struct wlr_screenshooter *screenshooter =
		calloc(1, sizeof(struct wlr_screenshooter));
	if (!screenshooter) {
		return NULL;
	}
	screenshooter->renderer = renderer;

	wl_list_init(&screenshooter->screenshots);
	wl_signal_init(&screenshooter->events.destroy);

	screenshooter->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &screenshooter->display_destroy);

	screenshooter->wl_global = wl_global_create(display,
		&orbital_screenshooter_interface, 1, screenshooter, screenshooter_bind);
	if (screenshooter->wl_global == NULL) {
		free(screenshooter);
		return NULL;
	}

	return screenshooter;
}
