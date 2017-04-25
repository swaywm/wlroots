#include <stdlib.h>
#include <stdint.h>
#include <wayland-server.h>
#include <assert.h>
#include "backend/wayland.h"
#include "common/log.h"

void wlr_wl_backend_free(struct wlr_wl_backend *backend) {
	if (!backend) {
		return;
	}
	// TODO: free more shit
	free(backend);
}

/*
 * Initializes the wayland backend. Opens a connection to a remote wayland
 * compositor and creates surfaces for each output, then registers globals on
 * the specified display.
 */
struct wlr_wl_backend *wlr_wl_backend_init(
		struct wl_display *display, size_t outputs) {
	assert(display);
	struct wlr_wl_backend *backend;
	if (!(backend = calloc(sizeof(struct wlr_wl_backend), 1))) {
		wlr_log(L_ERROR, "Could not allocate backend");
		goto error;
	}
	if (!(backend->outputs = list_create())) {
		wlr_log(L_ERROR, "Could not allocate output list");
		goto error;
	}
	backend->local_display = display;
	backend->remote_display = wl_display_connect(getenv("_WAYLAND_DISPLAY"));
	if (!backend->remote_display) {
		wlr_log(L_ERROR, "Could not connect to remote display");
		goto error;
	}
	if (!(backend->remote_registry = wl_display_get_registry(
			backend->remote_display))) {
		wlr_log(L_ERROR, "Could not obtain reference to remote registry");
		goto error;
	}
	wlr_wlb_registry_poll(backend);
	return backend;
error:
	wlr_wl_backend_free(backend);
	return NULL;
}
