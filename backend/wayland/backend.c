#include <stdlib.h>
#include <stdint.h>
#include <wayland-server.h>
#include <assert.h>
#include <wlr/backend/interface.h>
#include <wlr/types.h>
#include "backend/wayland.h"
#include "common/log.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

/*
 * Initializes the wayland backend. Opens a connection to a remote wayland
 * compositor and creates surfaces for each output, then registers globals on
 * the specified display.
 */
static bool wlr_wl_backend_init(struct wlr_backend_state* state) {
	wlr_log(L_INFO, "Initializating wayland backend");

	state->remote_display = wl_display_connect(getenv("_WAYLAND_DISPLAY"));
	if (!state->remote_display) {
		wlr_log_errno(L_ERROR, "Could not connect to remote display");
		return false;
	}

	if (!(state->registry = wl_display_get_registry(state->remote_display))) {
		wlr_log_errno(L_ERROR, "Could not obtain reference to remote registry");
		return false;
	}

	wlr_wl_registry_poll(state);
	if (!(state->compositor) || (!(state->shell))) {
		wlr_log_errno(L_ERROR, "Could not obtain retrieve required globals");
		return false;
	}

	wlr_egl_init(&state->egl, EGL_PLATFORM_WAYLAND_KHR, state->remote_display);
	for (size_t i = 0; i < state->num_outputs; ++i) {
		if(!(state->outputs[i] = wlr_wl_output_create(state, i))) {
			wlr_log_errno(L_ERROR, "Failed to create %zuth output", i);
			return false;
		}
	}

	return true;
}

static void wlr_wl_backend_destroy(struct wlr_backend_state *state) {
	if (!state) {
		return;
	}

	for (size_t i = 0; i < state->num_outputs; ++i) {
		wlr_output_destroy(state->outputs[i]);
	}

	wlr_egl_free(&state->egl);
	free(state->outputs);
	if (state->seat) wl_seat_destroy(state->seat);
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
		size_t num_outputs) {
	wlr_log(L_INFO, "Creating wayland backend");

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

	if (!(state->devices = list_create())) {
		wlr_log(L_ERROR, "Could not allocate devices list");
		goto error;
	}

	if (!(state->outputs = calloc(sizeof(void*), num_outputs))) {
		wlr_log(L_ERROR, "Could not allocate outputs list");
		goto error;
	}

	state->local_display = display;
	state->backend = backend;
	state->num_outputs = num_outputs;

	return backend;

error:
	if (state) {
		free(state->outputs);
		free(state->devices);
		free(state->devices);
	}
	free(state);
	free(backend);
	return NULL;
}
