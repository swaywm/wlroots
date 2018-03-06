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

	struct {
		xcb_atom_t wm_protocols;
		xcb_atom_t wm_delete_window;
		xcb_atom_t net_wm_name;
		xcb_atom_t utf8_string;
	} atoms;

	// The time we last received an event
	xcb_timestamp_t time;

	struct wl_listener display_destroy;
};

#endif
