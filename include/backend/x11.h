#ifndef BACKEND_X11_H
#define BACKEND_X11_H

#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/backend/x11.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>

#define XCB_EVENT_RESPONSE_TYPE_MASK 0x7f

#define X11_DEFAULT_REFRESH (60 * 1000) // 60 Hz

struct wlr_x11_backend;

struct wlr_x11_output {
	struct wlr_output wlr_output;
	struct wlr_x11_backend *x11;
	struct wl_list link; // wlr_x11_backend::outputs

	xcb_window_t win;
	EGLSurface surf;

	struct wlr_pointer pointer;
	struct wlr_input_device pointer_dev;

	struct wl_event_source *frame_timer;
	int frame_delay;
};

struct wlr_x11_backend {
	struct wlr_backend backend;
	struct wl_display *wl_display;
	bool started;

	Display *xlib_conn;
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;

	size_t requested_outputs;
	struct wl_list outputs; // wlr_x11_output::link

	struct wlr_keyboard keyboard;
	struct wlr_input_device keyboard_dev;

	struct wlr_egl egl;
	struct wlr_renderer *renderer;
	struct wl_event_source *event_source;

	struct {
		xcb_atom_t wm_protocols;
		xcb_atom_t wm_delete_window;
		xcb_atom_t net_wm_name;
		xcb_atom_t utf8_string;
	} atoms;

	// The time we last received an event
	xcb_timestamp_t time;

	// A blank cursor
	xcb_cursor_t cursor;

#ifdef WLR_HAS_XCB_XKB
	bool xkb_supported;
	uint8_t xkb_base_event;
	uint8_t xkb_base_error;
#endif

	struct wl_listener display_destroy;
};

struct wlr_x11_backend *get_x11_backend_from_backend(
	struct wlr_backend *wlr_backend);
struct wlr_x11_output *get_x11_output_from_window_id(
	struct wlr_x11_backend *x11, xcb_window_t window);

extern const struct wlr_keyboard_impl keyboard_impl;
extern const struct wlr_pointer_impl pointer_impl;
extern const struct wlr_input_device_impl input_device_impl;

void handle_x11_input_event(struct wlr_x11_backend *x11,
	xcb_generic_event_t *event);
void update_x11_pointer_position(struct wlr_x11_output *output,
	xcb_timestamp_t time);

void handle_x11_configure_notify(struct wlr_x11_output *output,
	xcb_configure_notify_event_t *event);

#endif
