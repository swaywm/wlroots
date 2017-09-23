#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include "rootston/server.h"
#include "rootston/desktop.h"
#include "rootston/config.h"

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_output *output = wl_container_of(listener, output, frame);
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(server->renderer, wlr_output);

	// TODO: render views

	wlr_renderer_end(server->renderer);
	wlr_output_swap_buffers(wlr_output);

	output->last_frame = desktop->last_frame = now;
}

void output_add_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop = wl_container_of(listener, desktop, output_add);
	struct roots_config *config = desktop->config;

	wlr_log(L_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(L_DEBUG, "%s %s %"PRId32"mm x %"PRId32"mm",
			wlr_output->make, wlr_output->model,
			wlr_output->phys_width, wlr_output->phys_height);
	if (wlr_output->modes->length > 0) {
		wlr_output_set_mode(wlr_output, wlr_output->modes->items[0]);
	}

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	output->frame.notify = output_frame_notify;
	wl_list_init(&output->frame.link);
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&desktop->outputs, &output->link);

	struct output_config *output_config = config_get_output(config, wlr_output);
	if (output_config) {
		wlr_output_transform(wlr_output, output_config->transform);
		wlr_output_layout_add(desktop->layout,
				wlr_output, output_config->x, output_config->y);
	} else {
		wlr_output_layout_add_auto(desktop->layout, wlr_output);
	}

	/* TODO: cursor
	example_config_configure_cursor(sample->config, sample->cursor,
		sample->compositor);

	// TODO the cursor must be set depending on which surface it is displayed
	// over which should happen in the compositor.
	if (!wlr_output_set_cursor(wlr_output, image->buffer,
			image->width, image->width, image->height)) {
		wlr_log(L_DEBUG, "Failed to set hardware cursor");
		return;
	}

	wlr_cursor_warp(sample->cursor, NULL, sample->cursor->x, sample->cursor->y);
	*/
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop = wl_container_of(listener, desktop, output_remove);
	struct roots_output *output = NULL, *_output;
	wl_list_for_each(_output, &desktop->outputs, link) {
		if (_output->wlr_output == wlr_output) {
			output = _output;
			break;
		}
	}
	if (!output) {
		return; // We are unfamiliar with this output
	}
	wlr_output_layout_remove(desktop->layout, output->wlr_output);
	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);
	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->resolution.link);
	free(output);
}
