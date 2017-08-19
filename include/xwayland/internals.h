#ifndef XWAYLAND_INTERNALS_H
#define XWAYLAND_INTERNALS_H
#include <xcb/xcb.h>
#include <wayland-server-core.h>
#include <wlr/xwayland.h>

struct wlr_xwm {
	struct wlr_xwayland *xwayland;
	struct wl_event_source *event_source;
	struct wl_listener surface_listener;

	xcb_connection_t *xcb_connection;
	xcb_screen_t *xcb_screen;
};

void unlink_sockets(int display);
int open_display_sockets(int socks[2]);

void xwm_destroy(struct wlr_xwm *xwm);
struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland);

#endif
