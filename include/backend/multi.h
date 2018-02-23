#ifndef BACKEND_MULTI_H
#define BACKEND_MULTI_H

#include <wayland-util.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>

struct wlr_multi_backend {
	struct wlr_backend backend;

	struct wl_list backends;

	struct wl_listener display_destroy;

	struct wlr_multi_backend_events {
		struct wl_signal backend_add;
		struct wl_signal backend_remove;
	} events;
};

#endif
