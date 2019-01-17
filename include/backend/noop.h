#ifndef BACKEND_NOOP_H
#define BACKEND_NOOP_H

#include <wlr/backend/noop.h>
#include <wlr/backend/interface.h>

struct wlr_noop_backend {
	struct wlr_backend backend;
	struct wl_display *display;
	struct wl_list outputs;
	bool started;
};

struct wlr_noop_output {
	struct wlr_output wlr_output;

	struct wlr_noop_backend *backend;
	struct wl_list link;
};

struct wlr_noop_backend *noop_backend_from_backend(
	struct wlr_backend *wlr_backend);

#endif
