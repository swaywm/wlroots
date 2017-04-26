#include <stdlib.h>
#include <wayland-client.h>
#include "wlr/wayland.h"
#include "wlr/common/list.h"

void wlr_wl_output_free(struct wlr_wl_output *output) {
	if (!output) return;
	if (output->wl_output) wl_output_destroy(output->wl_output);
	if (output->make) free(output->make);
	if (output->model) free(output->model);
	for (size_t i = 0; output->modes && i < output->modes->length; ++i) {
		free(output->modes->items[i]);
	}
	list_free(output->modes);
	free(output);
}
