#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"

static int dispatch_events(int fd, uint32_t mask, void *data) {
	struct wlr_backend_state *state = data;
	int count = 0;
	if (mask & WL_EVENT_READABLE) {
		count = wl_display_dispatch(state->remote_display);
	}
	if (mask & WL_EVENT_WRITABLE) {
		count = wl_display_flush(state->remote_display);
	}
	if (mask == 0) {
		count = wl_display_dispatch_pending(state->remote_display);
		wl_display_flush(state->remote_display);
	}
	return count;
}

/*
 * Initializes the wayland backend. Opens a connection to a remote wayland
 * compositor and creates surfaces for each output, then registers globals on
 * the specified display.
 */
static bool wlr_wl_backend_init(struct wlr_backend_state* state) {
	wlr_log(L_INFO, "Initializating wayland backend");

	state->remote_display = wl_display_connect(NULL);
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

	wlr_egl_init(&state->egl, EGL_PLATFORM_WAYLAND_EXT, state->remote_display);
	for (size_t i = 0; i < state->requested_outputs; ++i) {
		wlr_wl_output_create(state->backend);
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(state->local_display);
	int fd = wl_display_get_fd(state->remote_display);
	int events = WL_EVENT_READABLE | WL_EVENT_ERROR |
		WL_EVENT_HANGUP | WL_EVENT_WRITABLE;
	state->remote_display_src = wl_event_loop_add_fd(loop, fd, events,
			dispatch_events, state);
	wl_event_source_check(state->remote_display_src);

	return true;
}

static void wlr_wl_backend_destroy(struct wlr_backend_state *state) {
	if (!state) {
		return;
	}

	for (size_t i = 0; i < state->outputs->length; ++i) {
		wlr_output_destroy(state->outputs->items[i]);
	}

	for (size_t i = 0; i < state->devices->length; ++i) {
		wlr_input_device_destroy(state->devices->items[i]);
	}

	list_free(state->devices);
	list_free(state->outputs);

	wlr_egl_free(&state->egl);
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

bool wlr_backend_is_wl(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

struct wlr_output *wlr_wl_output_for_surface(struct wlr_backend_state *backend,
	struct wl_surface *surface) {
	for (size_t i = 0; i < backend->outputs->length; ++i) {
		struct wlr_output *output = backend->outputs->items[i];
		if(output->state->surface == surface)
			return output;
	}

	return NULL;
}

struct wlr_backend *wlr_wl_backend_create(struct wl_display *display) {
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

	if (!(state->outputs = list_create())) {
		wlr_log(L_ERROR, "Could not allocate outputs list");
		goto error;
	}

	state->local_display = display;
	state->backend = backend;
	return backend;

error:
	if (state) {
		list_free(state->devices);
		list_free(state->outputs);
	}
	free(state);
	free(backend);
	return NULL;
}
