#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/render.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
#include "rootston/config.h"
#include "rootston/server.h"

struct roots_server server = { 0 };

static void ready(struct wl_listener *listener, void *data) {
	if (server.config->startup_cmd != NULL) {
		const char *cmd = server.config->startup_cmd;
		pid_t pid = fork();
		if (pid < 0) {
			wlr_log(L_ERROR, "cannot execute binding command: fork() failed");
		} else if (pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		}
	}
}

int main(int argc, char **argv) {
	assert(server.config = roots_config_create_from_args(argc, argv));
	assert(server.wl_display = wl_display_create());
	assert(server.wl_event_loop = wl_display_get_event_loop(server.wl_display));

	assert(server.backend = wlr_backend_autocreate(server.wl_display));

	assert(server.renderer = wlr_gles2_renderer_create(server.backend));
	server.data_device_manager =
		wlr_data_device_manager_create(server.wl_display);
	wl_display_init_shm(server.wl_display);
	server.desktop = desktop_create(&server, server.config);
	server.input = input_create(&server, server.config);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_log_errno(L_ERROR, "Unable to open wayland socket");
		wlr_backend_destroy(server.backend);
		return 1;
	}

	wlr_log(L_INFO, "Running compositor on wayland display '%s'", socket);
	setenv("_WAYLAND_DISPLAY", socket, true);

	if (!wlr_backend_start(server.backend)) {
		wlr_log(L_ERROR, "Failed to start backend");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
#ifndef HAS_XWAYLAND
	ready(NULL, NULL);
#else
	if (server.desktop->xwayland != NULL) {
		struct roots_seat *xwayland_seat =
			input_get_seat(server.input, ROOTS_CONFIG_DEFAULT_SEAT_NAME);
		wlr_xwayland_set_seat(server.desktop->xwayland, xwayland_seat->seat);
		wl_signal_add(&server.desktop->xwayland->events.ready,
			&server.desktop->xwayland_ready);
		server.desktop->xwayland_ready.notify = ready;
	} else {
		ready(NULL, NULL);
	}
#endif

	wl_display_run(server.wl_display);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}
