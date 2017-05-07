#include <wayland-server.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common/log.h"
#include "backend.h"

struct wlr_backend *wlr_backend_create(const struct wlr_backend_impl *impl,
		struct wlr_backend_state *state) {
	struct wlr_backend *backend = calloc(1, sizeof(struct wlr_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	backend->state = state;
	backend->impl = impl;
	wl_signal_init(&backend->events.output_add);
	wl_signal_init(&backend->events.output_remove);
	wl_signal_init(&backend->events.output_frame);
	wl_signal_init(&backend->events.keyboard_add);
	wl_signal_init(&backend->events.keyboard_remove);
	wl_signal_init(&backend->events.pointer_add);
	wl_signal_init(&backend->events.pointer_remove);
	wl_signal_init(&backend->events.touch_add);
	wl_signal_init(&backend->events.touch_remove);
	return backend;
}

bool wlr_backend_init(struct wlr_backend *backend) {
	return backend->impl->init(backend->state);
}

void wlr_backend_destroy(struct wlr_backend *backend) {
	backend->impl->destroy(backend->state);
	// TODO: Free anything else?
	free(backend);
}
