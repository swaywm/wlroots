#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/noop.h"
#include "util/signal.h"

static struct wlr_noop_output *noop_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_noop(wlr_output));
	return (struct wlr_noop_output *)wlr_output;
}

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	// empty
}

static bool output_make_current(struct wlr_output *wlr_output, int *buffer_age) {
	return true;
}

static bool output_swap_buffers(struct wlr_output *wlr_output,
		pixman_region32_t *damage) {
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_noop_output *output =
		noop_output_from_output(wlr_output);

	wl_list_remove(&output->link);

	free(output);
}

static const struct wlr_output_impl output_impl = {
	.transform = output_transform,
	.destroy = output_destroy,
	.make_current = output_make_current,
	.swap_buffers = output_swap_buffers,
};

bool wlr_output_is_noop(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

struct wlr_output *wlr_noop_add_output(struct wlr_backend *wlr_backend) {
	struct wlr_noop_backend *backend = noop_backend_from_backend(wlr_backend);

	struct wlr_noop_output *output = calloc(1, sizeof(struct wlr_noop_output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_noop_output");
		return NULL;
	}
	output->backend = backend;
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
			backend->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	strncpy(wlr_output->make, "noop", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "noop", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "NOOP-%d",
		wl_list_length(&backend->outputs) + 1);

	wl_list_insert(&backend->outputs, &output->link);

	if (backend->started) {
		wlr_output_update_enabled(wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	}

	return wlr_output;
}
