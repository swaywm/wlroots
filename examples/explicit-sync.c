#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/timeline.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_explicit_synchronization_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

/* Simple compositor with explicit synchronization support. Input is
 * unimplemented.
 *
 * New surfaces are stacked on top of the existing ones as they appear. */

struct server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync_v1;

	struct wl_list outputs;
	struct wl_list surfaces;

	struct wl_listener new_output;
	struct wl_listener new_surface;
};

struct surface {
	struct wlr_surface *wlr;
	struct wl_list link;

	struct wlr_render_timeline *timeline;

	struct wl_listener destroy;
};

struct output {
	struct wl_list link;
	struct server *server;
	struct wlr_output *wlr;

	struct wlr_render_timeline *in_timeline, *out_timeline;

	struct wl_listener frame;
};

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct output *output = wl_container_of(listener, output, frame);
	struct wlr_renderer *renderer = output->server->renderer;

	if (!wlr_output_attach_render(output->wlr, NULL)) {
		return;
	}

	wlr_renderer_begin(renderer, output->wlr->width, output->wlr->height);
	wlr_renderer_clear(renderer, (float[]){0.25f, 0.25f, 0.25f, 1});

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	int pos = 0;
	struct surface *surface;
	wl_list_for_each(surface, &output->server->surfaces, link) {
		pos += 50;

		struct wlr_texture *texture = wlr_surface_get_texture(surface->wlr);
		if (texture == NULL) {
			continue;
		}

		uint64_t surface_point = surface->wlr->current.seq;
		if (!wlr_linux_explicit_synchronization_v1_signal_surface_timeline(
				output->server->explicit_sync_v1, surface->wlr,
				surface->timeline, surface_point)) {
			wlr_log(WLR_ERROR, "Failed to signal surface timeline");
			continue;
		}
		if (!wlr_renderer_wait_timeline(renderer,
				surface->timeline, surface_point)) {
			wlr_log(WLR_ERROR, "Failed to wait for surface timeline");
			continue;
		}

		wlr_render_texture(renderer, texture, output->wlr->transform_matrix,
			pos, pos, 1.0);

		wlr_surface_send_frame_done(surface->wlr, &now);
	}

	uint64_t output_point = output->wlr->commit_seq;
	if (!wlr_renderer_signal_timeline(renderer, output->in_timeline,
			output_point)) {
		wlr_log(WLR_ERROR, "Failed to signal renderer timeline");
		return;
	}

	wlr_renderer_end(renderer);

	wlr_output_set_wait_timeline(output->wlr,
		output->in_timeline, output_point);
	wlr_output_set_signal_timeline(output->wlr,
		output->out_timeline, output_point);

	if (!wlr_output_commit(output->wlr)) {
		wlr_log(WLR_ERROR, "Failed to commit output");
		return;
	}

	wl_list_for_each(surface, &output->server->surfaces, link) {
		if (!wlr_linux_explicit_synchronization_v1_wait_surface_timeline(
				output->server->explicit_sync_v1, surface->wlr,
				output->out_timeline, output_point)) {
			wlr_log(WLR_ERROR, "Failed to wait for surface timeline");
		}
	}
}

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	struct server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	int drm_fd = wlr_renderer_get_drm_fd(server->renderer);
	struct wlr_render_timeline *in_timeline = wlr_render_timeline_create(drm_fd);
	struct wlr_render_timeline *out_timeline = wlr_render_timeline_create(drm_fd);
	if (in_timeline == NULL || out_timeline == NULL) {
		return;
	}

	struct output *output =
		calloc(1, sizeof(struct output));
	output->wlr = wlr_output;
	output->server = server;
	output->in_timeline = in_timeline;
	output->out_timeline = out_timeline;
	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);

	if (!wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
		wlr_output_set_mode(wlr_output, mode);
		wlr_output_commit(wlr_output);
	}

	wlr_output_create_global(wlr_output);
}

static void surface_handle_destroy(struct wl_listener *listener, void *data) {
	struct surface *surface = wl_container_of(listener, surface, destroy);
	wlr_render_timeline_destroy(surface->timeline);
	wl_list_remove(&surface->destroy.link);
	wl_list_remove(&surface->link);
	free(surface);
}

static void server_handle_new_surface(struct wl_listener *listener,
		void *data) {
	struct server *server = wl_container_of(listener, server, new_surface);
	struct wlr_surface *wlr_surface = data;

	int drm_fd = wlr_renderer_get_drm_fd(server->renderer);
	struct wlr_render_timeline *timeline = wlr_render_timeline_create(drm_fd);
	if (timeline == NULL) {
		return;
	}

	struct surface *surface = calloc(1, sizeof(struct surface));
	surface->wlr = wlr_surface;
	surface->timeline = timeline;
	surface->destroy.notify = surface_handle_destroy;
	wl_signal_add(&wlr_surface->events.destroy, &surface->destroy);

	wl_list_insert(&server->surfaces, &surface->link);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);

	const char *startup_cmd = NULL;
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
	server.display = wl_display_create();
	server.backend = wlr_backend_autocreate(server.display);
	server.renderer = wlr_backend_get_renderer(server.backend);
	wlr_renderer_init_wl_display(server.renderer, server.display);

	struct wlr_compositor *compositor =
		wlr_compositor_create(server.display, server.renderer);

	wlr_xdg_shell_create(server.display);

	server.explicit_sync_v1 =
		wlr_linux_explicit_synchronization_v1_create(server.display);

	wl_list_init(&server.outputs);
	wl_list_init(&server.surfaces);

	server.new_output.notify = server_handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.new_surface.notify = server_handle_new_surface;
	wl_signal_add(&compositor->events.new_surface, &server.new_surface);

	const char *socket = wl_display_add_socket_auto(server.display);
	if (!socket) {
		wl_display_destroy(server.display);
		return EXIT_FAILURE;
	}

	if (!wlr_backend_start(server.backend)) {
		wl_display_destroy(server.display);
		return EXIT_FAILURE;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd != NULL) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
		socket);
	wl_display_run(server.display);

	wl_display_destroy_clients(server.display);
	wl_display_destroy(server.display);
	return EXIT_SUCCESS;
}
