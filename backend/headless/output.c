#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
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

	output->frame_delay = 1000000 / refresh;

	wlr_output_update_custom_mode(&output->wlr_output, width, height, refresh);
	return true;
}

static bool output_test(struct wlr_output *wlr_output) {
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		wlr_log(WLR_DEBUG, "Cannot disable a headless output");
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(wlr_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
	}

	return true;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (!output_test(wlr_output)) {
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		if (!output_set_custom_mode(wlr_output,
				wlr_output->pending.custom_mode.width,
				wlr_output->pending.custom_mode.height,
				wlr_output->pending.custom_mode.refresh)) {
			return false;
		}
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		assert(wlr_output->pending.buffer_type ==
			WLR_OUTPUT_STATE_BUFFER_SCANOUT);

		wlr_buffer_unlock(output->front_buffer);
		output->front_buffer = wlr_buffer_lock(wlr_output->pending.buffer);

		wlr_output_send_present(wlr_output, NULL);
	}

	return true;
}

static bool output_export_dmabuf(struct wlr_output *wlr_output,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (!output->front_buffer) {
		return false;
	}

	struct wlr_dmabuf_attributes tmp;
	if (!wlr_buffer_get_dmabuf(output->front_buffer, &tmp)) {
		return false;
	}

	return wlr_dmabuf_attributes_copy(attribs, &tmp);
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);
	wl_list_remove(&output->link);
	wl_event_source_remove(output->frame_timer);
	wlr_buffer_unlock(output->front_buffer);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.commit = output_commit,
	.export_dmabuf = output_export_dmabuf,
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

	output_set_custom_mode(wlr_output, width, height, 0);
	strncpy(wlr_output->make, "headless", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "headless", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "HEADLESS-%zd",
		++backend->last_output_num);

	char description[128];
	snprintf(description, sizeof(description),
		"Headless output %zd", backend->last_output_num);
	wlr_output_set_description(wlr_output, description);

	struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&backend->outputs, &output->link);

	if (backend->started) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	}

	return wlr_output;
}
