#define _POSIX_C_SOURCE 200112L
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layer.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

/* Simple compositor making use of the output layers API. The compositor will
 * attempt to display client surfaces with output layers. Input is
 * unimplemented.
 *
 * New surfaces are stacked on top of the existing ones as they appear.
 * Surfaces that don't make it into an output layer are rendered as usual. */

struct server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;

	struct wl_list outputs;

	struct wl_listener new_output;
	struct wl_listener new_surface;
};

struct output_surface {
	struct wlr_surface *wlr_surface;
	struct wlr_output_layer *layer;
	struct wl_list link;

	int x, y;

	bool first_commit, prev_layer_accepted;

	struct wl_listener destroy;
	struct wl_listener commit;
};

struct output {
	struct wl_list link;
	struct server *server;
	struct wlr_output *wlr_output;
	struct wl_list surfaces;

	struct wl_listener frame;
};

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct output *output =
		wl_container_of(listener, output, frame);
	struct wlr_renderer *renderer = output->server->renderer;

	if (!wlr_output_test(output->wlr_output)) {
		return;
	}

	if (!wlr_output_attach_render(output->wlr_output, NULL)) {
		return;
	}

	int width, height;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	wlr_renderer_begin(renderer, width, height);
	wlr_renderer_clear(renderer, (float[4]){ 0.3, 0.3, 0.3, 1.0 });

	struct output_surface *output_surface;
	wl_list_for_each(output_surface, &output->surfaces, link) {
		struct wlr_surface *wlr_surface = output_surface->wlr_surface;

		if (wlr_surface->buffer == NULL || output_surface->layer->accepted) {
			continue;
		}

		struct wlr_texture *texture = wlr_surface_get_texture(wlr_surface);
		if (texture == NULL) {
			continue;
		}

		wlr_render_texture(renderer, texture, output->wlr_output->transform_matrix,
			output_surface->x, output_surface->y, 1.0);
	}

	wlr_renderer_end(renderer);

	wlr_output_commit(output->wlr_output);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	wl_list_for_each(output_surface, &output->surfaces, link) {
		wlr_surface_send_frame_done(output_surface->wlr_surface, &now);

		if (output_surface->wlr_surface->buffer == NULL) {
			continue;
		}

		if ((output_surface->first_commit ||
				!output_surface->prev_layer_accepted) &&
				output_surface->layer->accepted) {
			wlr_log(WLR_DEBUG, "Scanning out wlr_surface %p on output '%s'",
				output_surface->wlr_surface, output->wlr_output->name);
		}
		if ((output_surface->first_commit ||
				output_surface->prev_layer_accepted) &&
				!output_surface->layer->accepted) {
			wlr_log(WLR_DEBUG, "Cannot scan out wlr_surface %p on output '%s'",
				output_surface->wlr_surface, output->wlr_output->name);
		}
		output_surface->prev_layer_accepted = output_surface->layer->accepted;
		output_surface->first_commit = false;
	}
}

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	struct output *output =
		calloc(1, sizeof(struct output));
	output->wlr_output = wlr_output;
	output->server = server;
	wl_list_init(&output->surfaces);
	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);

	if (!wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode =
			wl_container_of(wlr_output->modes.prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
		wlr_output_commit(wlr_output);
	}

	wlr_output_create_global(wlr_output);
}

static void output_surface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct output_surface *output_surface =
		wl_container_of(listener, output_surface, destroy);
	wlr_output_layer_remove(output_surface->layer);
	wl_list_remove(&output_surface->destroy.link);
	wl_list_remove(&output_surface->commit.link);
	wl_list_remove(&output_surface->link);
	free(output_surface);
}

static void output_surface_handle_commit(struct wl_listener *listener,
		void *data) {
	struct output_surface *output_surface =
		wl_container_of(listener, output_surface, commit);
	struct wlr_buffer *buffer = NULL;
	if (output_surface->wlr_surface->buffer != NULL) {
		buffer = &output_surface->wlr_surface->buffer->base;
	}
	wlr_output_layer_attach_buffer(output_surface->layer, buffer);
}

static void server_handle_new_surface(struct wl_listener *listener,
		void *data) {
	struct server *server = wl_container_of(listener, server, new_surface);
	struct wlr_surface *wlr_surface = data;

	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct output_surface *output_surface =
			calloc(1, sizeof(struct output_surface));
		output_surface->wlr_surface = wlr_surface;
		output_surface->destroy.notify = output_surface_handle_destroy;
		wl_signal_add(&wlr_surface->events.destroy, &output_surface->destroy);
		output_surface->commit.notify = output_surface_handle_commit;
		wl_signal_add(&wlr_surface->events.commit, &output_surface->commit);

		output_surface->layer = wlr_output_layer_create(output->wlr_output);
		int pos = 50 * wl_list_length(&output->surfaces);
		wlr_output_layer_move(output_surface->layer, pos, pos);

		output_surface->x = output_surface->y = pos;
		output_surface->first_commit = true;

		wl_list_insert(output->surfaces.prev, &output_surface->link);
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);

	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("usage: %s [-s startup-command]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (optind < argc) {
		printf("usage: %s [-s startup-command]\n", argv[0]);
		return EXIT_FAILURE;
	}

	struct server server = {0};
	server.wl_display = wl_display_create();
	server.backend = wlr_backend_autocreate(server.wl_display, NULL);
	server.renderer = wlr_backend_get_renderer(server.backend);
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	struct wlr_compositor *compositor =
		wlr_compositor_create(server.wl_display, server.renderer);

	wlr_xdg_shell_create(server.wl_display);

	wl_list_init(&server.outputs);

	server.new_output.notify = server_handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_surface.notify = server_handle_new_surface;
	wl_signal_add(&compositor->events.new_surface, &server.new_surface);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wl_display_destroy(server.wl_display);
		return EXIT_FAILURE;
	}

	if (!wlr_backend_start(server.backend)) {
		wl_display_destroy(server.wl_display);
		return EXIT_FAILURE;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	setenv("WAYLAND_DEBUG", "", true);
	if (startup_cmd != NULL) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
		socket);
	wl_display_run(server.wl_display);

	wl_display_destroy_clients(server.wl_display);
	wl_display_destroy(server.wl_display);
	return EXIT_SUCCESS;
}
