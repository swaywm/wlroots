#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"
#include "xdg-shell-unstable-v6-client-protocol.h"

static void xdg_shell_handle_ping(void *data, struct zxdg_shell_v6 *shell,
		uint32_t serial) {
	zxdg_shell_v6_pong(shell, serial);
}

static const struct zxdg_shell_v6_listener xdg_shell_listener = {
	xdg_shell_handle_ping,
};


static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct wlr_wl_backend *backend = data;
	wlr_log(WLR_DEBUG, "Remote wayland global: %s v%d", interface, version);

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		backend->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, version);
	} else if (strcmp(interface, zxdg_shell_v6_interface.name) == 0) {
		backend->shell = wl_registry_bind(registry, name,
				&zxdg_shell_v6_interface, version);
		zxdg_shell_v6_add_listener(backend->shell, &xdg_shell_listener, NULL);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		backend->shm = wl_registry_bind(registry, name,
				&wl_shm_interface, version);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		backend->seat = wl_registry_bind(registry, name,
				&wl_seat_interface, version);
		wl_seat_add_listener(backend->seat, &seat_listener, backend);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// TODO
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove
};

void poll_wl_registry(struct wlr_wl_backend *backend) {
	wl_registry_add_listener(backend->registry, &registry_listener, backend);
	wl_display_dispatch(backend->remote_display);
	wl_display_roundtrip(backend->remote_display);
}
