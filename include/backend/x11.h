#ifndef WLR_X11_H
#define WLR_X11_H

#include <xcb/xcb.h>
#include <X11/Xlib-xcb.h>
#include <wayland-server.h>
#include <wlr/egl.h>

struct wlr_x11_backend {
	struct wlr_backend backend;

	Display *xlib_conn;
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t win;

	struct wlr_egl egl;
	struct wl_event_source *event_source;
};

#endif
