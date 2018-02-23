#ifndef BACKEND_X11_H
#define BACKEND_X11_H

#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/render/egl.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>

struct wlr_x11_backend;

struct wlr_x11_output {
	struct wlr_output wlr_output;
	struct wlr_x11_backend *x11;

	xcb_window_t win;
	EGLSurface surf;
};

struct wlr_x11_atom {
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;
};

struct wlr_x11_backend {
	struct wlr_backend backend;
	struct wl_display *wl_display;

	Display *xlib_conn;
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;

	struct wlr_x11_output output;

	struct wlr_keyboard keyboard;
	struct wlr_input_device keyboard_dev;

	struct wlr_pointer pointer;
	struct wlr_input_device pointer_dev;

	struct wlr_egl egl;
	struct wlr_renderer *renderer;
	struct wl_event_source *event_source;
	struct wl_event_source *frame_timer;

	struct wlr_x11_backend_events {
		struct wlr_x11_atom wm_protocols;
		struct wlr_x11_atom wm_delete_window;
		struct wlr_x11_atom net_wm_name;
		struct wlr_x11_atom utf8_string;
	} atoms;

	// The time we last received an event
	xcb_timestamp_t time;

	struct wl_listener display_destroy;
};

#endif
