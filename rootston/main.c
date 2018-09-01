#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/config.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "rootston/config.h"
#include "rootston/server.h"

#include <wlr/render/vulkan.h>
#include <wlr/render/gles2.h>

struct roots_server server = { 0 };

int main(int argc, char **argv) {
	wlr_log_init(WLR_DEBUG, NULL);
	server.config = roots_config_create_from_args(argc, argv);
	server.wl_display = wl_display_create();
	server.wl_event_loop = wl_display_get_event_loop(server.wl_display);
	assert(server.config && server.wl_display && server.wl_event_loop);

	server.backend = wlr_backend_autocreate(server.wl_display, NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "could not start backend");
		return 1;
	}

	server.renderer = wlr_backend_get_renderer(server.backend);
	assert(server.renderer);
	server.data_device_manager =
		wlr_data_device_manager_create(server.wl_display);
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);
	server.desktop = desktop_create(&server, server.config);
	server.input = input_create(&server, server.config);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_log_errno(WLR_ERROR, "Unable to open wayland socket");
		wlr_backend_destroy(server.backend);
		return 1;
	}

	wlr_log(WLR_INFO, "Running compositor on wayland display '%s'", socket);
	setenv("_WAYLAND_DISPLAY", socket, true);

	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "Failed to start backend");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
#ifdef WLR_HAS_XWAYLAND
	if (server.desktop->xwayland != NULL) {
		struct roots_seat *xwayland_seat =
			input_get_seat(server.input, ROOTS_CONFIG_DEFAULT_SEAT_NAME);
		wlr_xwayland_set_seat(server.desktop->xwayland, xwayland_seat->seat);
	}
#endif

	if (server.config->startup_cmd != NULL) {
		const char *cmd = server.config->startup_cmd;
		pid_t pid = fork();
		if (pid < 0) {
			wlr_log(WLR_ERROR, "cannot execute binding command: fork() failed");
		} else if (pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
		}
	}

	wl_display_run(server.wl_display);
#ifdef WLR_HAS_XWAYLAND
	wlr_xwayland_destroy(server.desktop->xwayland);
#endif
	wl_display_destroy_clients(server.wl_display);
	wl_display_destroy(server.wl_display);
	return 0;
}
