#ifndef _WLR_BACKEND_H
#define _WLR_BACKEND_H

#include <wayland-server.h>
#include <wlr/session.h>

struct wlr_backend_impl;
struct wlr_backend_state;

struct wlr_backend {
	const struct wlr_backend_impl *impl;
	struct wlr_backend_state *state;

	struct {
		struct wl_signal output_add;
		struct wl_signal output_remove;
		struct wl_signal keyboard_add;
		struct wl_signal keyboard_remove;
		struct wl_signal pointer_add;
		struct wl_signal pointer_remove;
		struct wl_signal touch_add;
		struct wl_signal touch_remove;
	} events;
};

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display,
		struct wlr_session *session);
bool wlr_backend_init(struct wlr_backend *backend);
void wlr_backend_destroy(struct wlr_backend *backend);

#endif
