#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "backend/headless.h"
#include "util/signal.h"

static struct wlr_headless_output *headless_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_headless(wlr_output));
	return (struct wlr_headless_output *)wlr_output;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, int32_t width,
		int32_t height, int32_t refresh) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (refresh <= 0) {
		refresh = HEADLESS_DEFAULT_REFRESH;
	}

	wlr_render_surface_resize(output->render_surface, width, height);
	output->frame_delay = 1000000 / refresh;

	wlr_output_update_custom_mode(&output->wlr_output, width, height, refresh);
	return true;
}

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);
	output->wlr_output.transform = transform;
}

static struct wlr_render_surface *output_get_render_surface(
		struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);
	return output->render_surface;
}

static bool output_swap_buffers(struct wlr_output *wlr_output,
		pixman_region32_t *damage) {
	struct wlr_headless_output *output =
		(struct wlr_headless_output *)wlr_output;
	if (!wlr_render_surface_swap_buffers(output->render_surface, NULL)) {
		return false;
	}

	wlr_output_send_present(wlr_output, NULL);
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	wl_list_remove(&output->link);

	wl_event_source_remove(output->frame_timer);
	wlr_render_surface_destroy(output->render_surface);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.get_render_surface = output_get_render_surface,
	.swap_buffers = output_swap_buffers,
};

bool wlr_output_is_headless(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static int signal_frame(void *data) {
	struct wlr_headless_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	return 0;
}

struct wlr_output *wlr_headless_add_output(struct wlr_backend *wlr_backend,
		unsigned int width, unsigned int height) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);

	struct wlr_headless_output *output =
		calloc(1, sizeof(struct wlr_headless_output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_output");
		return NULL;
	}
	output->backend = backend;
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	output->render_surface = wlr_renderer_create_render_surface(
		backend->renderer, NULL, width, height);
	if (!output->render_surface) {
		wlr_log(WLR_ERROR, "Failed to create wlr_render_surface");
		goto error;
	}

	output_set_custom_mode(wlr_output, width, height, 0);
	strncpy(wlr_output->make, "headless", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "headless", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "HEADLESS-%d",
		wl_list_length(&backend->outputs) + 1);

	wlr_renderer_begin(backend->renderer, output->render_surface);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&backend->outputs, &output->link);

	if (backend->started) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	}

	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
