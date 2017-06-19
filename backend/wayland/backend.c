#include <stdlib.h>
#include <stdint.h>
#include <wayland-server.h>
#include <assert.h>
#include <wlr/backend/interface.h>
#include "backend/wayland.h"
#include "common/log.h"

/*
 * Initializes the wayland backend. Opens a connection to a remote wayland
 * compositor and creates surfaces for each output, then registers globals on
 * the specified display.
 */
static bool wlr_wl_backend_init(struct wlr_backend_state* state) {
	state->remote_display = wl_display_connect(getenv("_WAYLAND_DISPLAY"));
	if (!state->remote_display) {
		wlr_log(L_ERROR, "Could not connect to remote display");
		return false;
	}

	if (!(state->registry = wl_display_get_registry(state->remote_display))) {
		wlr_log(L_ERROR, "Could not obtain reference to remote registry");
		return false;
	}

	wlr_wlb_registry_poll(state);
	return true;
}

static void wlr_wl_backend_destroy(struct wlr_backend_state *state) {
	if (!state) {
		return;
	}

	// TODO: Free surfaces
	for (size_t i = 0; state->outputs && i < state->outputs->length; ++i) {
		struct wlr_wl_output *output = state->outputs->items[i];
		wlr_wl_output_free(output);
	}

	list_free(state->outputs);
	if (state->seat) wlr_wl_seat_free(state->seat);
	if (state->shm) wl_shm_destroy(state->shm);
	if (state->shell) wl_shell_destroy(state->shell);
	if (state->compositor) wl_compositor_destroy(state->compositor);
	if (state->registry) wl_registry_destroy(state->registry);
	if (state->remote_display) wl_display_disconnect(state->remote_display);
	free(state);
}

static struct wlr_backend_impl backend_impl = {
	.init = wlr_wl_backend_init,
	.destroy = wlr_wl_backend_destroy
};


struct wlr_backend *wlr_wl_backend_create(struct wl_display *display,
		size_t outputs) {
	wlr_log(L_INFO, "Initalizing wayland backend");

	struct wlr_backend_state *state = calloc(1, sizeof(struct wlr_backend_state));
	if (!state) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	struct wlr_backend *backend = wlr_backend_create(&backend_impl, state);
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	if (!(state->outputs = list_create())) {
		wlr_log(L_ERROR, "Could not allocate output list");
		goto error;
	}
	state->local_display = display;

	return backend;

error:
	free(state);
	free(backend);
	return NULL;
}
