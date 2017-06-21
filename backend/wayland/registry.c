#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct wlr_backend_state *state = data;
	wlr_log(L_DEBUG, "Remote wayland global: %s v%d", interface, version);

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shell_interface.name) == 0) {
		state->shell = wl_registry_bind(registry, name,
				&wl_shell_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name,
				&wl_shm_interface, version);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->seat = wl_registry_bind(registry, name,
				&wl_seat_interface, version);
		wl_seat_add_listener(state->seat, &seat_listener, state);
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

void wlr_wl_registry_poll(struct wlr_backend_state *state) {
	wl_registry_add_listener(state->registry, &registry_listener, state);
	wl_display_dispatch(state->remote_display);
	wl_display_roundtrip(state->remote_display);
}
