#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "backend/wayland.h"
#include "common/log.h"

static void registry_wl_seat(struct wlr_wl_backend *backend,
		struct wl_seat *wl_seat, struct wl_registry *registry, uint32_t version) {
	struct wlr_wl_seat *seat;
	if (!(seat = calloc(sizeof(struct wlr_wl_seat), 1))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_seat");
		goto error;
	}
	if (!(seat->keyboards = list_create())) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_seat");
		goto error;
	}
	if (!(seat->pointers = list_create())) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_pointer");
		goto error;
	}
	seat->wl_seat = wl_seat;
	wl_seat_add_listener(wl_seat, &seat_listener, seat);
error:
	//wlr_wl_seat_free(seat); TODO
	return;
}

static void registry_wl_output(struct wlr_wl_backend *backend,
		struct wl_output *wl_output, struct wl_registry *registry, uint32_t version) {
	struct wlr_wl_output *output;
	if (!(output = calloc(sizeof(struct wlr_wl_output), 1))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_output");
		return;
	}
	output->wl_output = wl_output;
	output->scale = 1;
	list_add(backend->outputs, output);
	wl_output_set_user_data(wl_output, backend);
	wl_output_add_listener(wl_output, &output_listener, output);
}

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct wlr_wl_backend *backend = data;
	wlr_log(L_DEBUG, "Remote wayland global: %s v%d", interface, version);

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		backend->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, version);
	} else if (strcmp(interface, wl_shell_interface.name) == 0) {
		backend->shell = wl_registry_bind(registry, name,
				&wl_shell_interface, version);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		backend->shm = wl_registry_bind(registry, name,
				&wl_shm_interface, version);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *wl_seat = wl_registry_bind(registry, name,
				&wl_seat_interface, version);
		registry_wl_seat(backend, wl_seat, registry, version);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *wl_output = wl_registry_bind(registry, name,
				&wl_output_interface, version);
		registry_wl_output(backend, wl_output, registry, version);
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

void wlr_wlb_registry_poll(struct wlr_wl_backend *backend) {
	wl_registry_add_listener(backend->registry,
			&registry_listener, backend->registry);
	wl_display_dispatch(backend->remote_display);
	wl_display_roundtrip(backend->remote_display);
}
