#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/types.h>
#include "types.h"
#include "backend/wayland.h"
#include "common/log.h"

// TODO
static void wlr_wl_output_enable(struct wlr_output_state *output, bool enable) {
}

static bool wlr_wl_output_set_mode(struct wlr_output_state *output,
		struct wlr_output_mode *mode) {
	return false;
}

static void wlr_wl_output_transform(struct wlr_output_state *output,
		enum wl_output_transform transform) {
}

static bool wlr_wl_output_set_cursor(struct wlr_output_state *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	return false;
}

static bool wlr_wl_output_move_cursor(struct wlr_output_state *output,
		int x, int y) {
	return false;
}

static void wlr_wl_output_destroy(struct wlr_output_state *output) {
	free(output);
}

static struct wlr_output_impl output_impl = {
	.enable = wlr_wl_output_enable,
	.set_mode = wlr_wl_output_set_mode,
	.transform = wlr_wl_output_transform,
	.set_cursor = wlr_wl_output_set_cursor,
	.move_cursor = wlr_wl_output_move_cursor,
	.destroy = wlr_wl_output_destroy,
};

static void registry_wl_output(struct wlr_backend_state *state,
	struct wl_output *wl_output, struct wl_registry *registry, uint32_t version) {
	struct wlr_output_state *output;
	if (!(output = calloc(sizeof(struct wlr_output_state), 1))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_output");
		return;
	}

	struct wlr_output *wlr_output = wlr_output_create(&output_impl, output);
	if (!wlr_output) {
		free(state);
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return;
	}

	output->output = wl_output;
	list_add(state->outputs, output);
	wl_output_add_listener(wl_output, &output_listener, output);
	wl_signal_emit(&state->backend->events.output_add, wlr_output);
	return;
}

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
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *wl_output = wl_registry_bind(registry, name,
				&wl_output_interface, version);
		registry_wl_output(state, wl_output, registry, version);
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

void wlr_wlb_registry_poll(struct wlr_backend_state *state) {
	wl_registry_add_listener(state->registry, &registry_listener, state);
	wl_display_dispatch(state->remote_display);
	wl_display_roundtrip(state->remote_display);
}
