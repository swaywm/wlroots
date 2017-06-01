#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types.h>
#include <wlr/common/list.h>
#include "types.h"

struct wlr_output *wlr_output_create(struct wlr_output_impl *impl,
		struct wlr_output_state *state) {
	struct wlr_output *output = calloc(1, sizeof(struct wlr_output));
	output->impl = impl;
	output->state = state;
	output->modes = list_create();
	wl_signal_init(&output->events.frame);
	return output;
}

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) return;
	output->impl->destroy(output->state);
	if (output->make) free(output->make);
	if (output->model) free(output->model);
	for (size_t i = 0; output->modes && i < output->modes->length; ++i) {
		free(output->modes->items[i]);
	}
	list_free(output->modes);
	free(output);
}

bool wlr_output_set_mode(struct wlr_output *output, struct wlr_output_mode *mode) {
	return output->impl->set_mode(output->state, mode);
}

void wlr_output_enable(struct wlr_output *output, bool enable) {
	output->impl->enable(output->state, enable);
}
