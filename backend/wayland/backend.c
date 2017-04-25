#include <stdlib.h>
#include <stdint.h>
#include <wayland-server.h>
#include "backend/wayland.h"

struct wlr_wayland_backend *wayland_backend_init(struct wl_display *display,
		size_t outputs) {
	struct wlr_wayland_backend *backend = calloc(
			sizeof(struct wlr_wayland_backend), 1);
	backend->local_display = display;
	// TODO: obtain reference to remote display
	return backend;
}
