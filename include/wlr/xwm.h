#ifndef XWAYLAND_INTERNALS_H
#define XWAYLAND_INTERNALS_H

#include <xcb/render.h>
#include <wayland-server-core.h>
#include <wlr/xwayland.h>

enum atom_name {
	WL_SURFACE_ID,
	WM_DELETE_WINDOW,
	WM_PROTOCOLS,
	WM_HINTS,
	WM_NORMAL_HINTS,
	WM_SIZE_HINTS,
	MOTIF_WM_HINTS,
	UTF8_STRING,
	WM_S0,
	NET_SUPPORTED,
	NET_WM_S0,
	NET_WM_PID,
	NET_WM_NAME,
	NET_WM_STATE,
	NET_WM_WINDOW_TYPE,
	WM_TAKE_FOCUS,
	WINDOW,
	_NET_ACTIVE_WINDOW,
	_NET_WM_MOVERESIZE,
	_NET_WM_NAME,
	_NET_SUPPORTING_WM_CHECK,
	_NET_WM_STATE_FULLSCREEN,
	_NET_WM_STATE_MAXIMIZED_VERT,
	_NET_WM_STATE_MAXIMIZED_HORZ,
	WM_STATE,
	CLIPBOARD,
	PRIMARY,
	WL_SELECTION,
	TARGETS,
	CLIPBOARD_MANAGER,
	INCR,
	TEXT,
	TIMESTAMP,
	ATOM_LAST,
};

extern const char *atom_map[ATOM_LAST];

enum net_wm_state_action {
	NET_WM_STATE_REMOVE = 0,
	NET_WM_STATE_ADD = 1,
	NET_WM_STATE_TOGGLE = 2,
};

struct wlr_xwm_selection {
	struct wlr_xwm *xwm;
	xcb_atom_t atom;
	xcb_window_t window;
	xcb_selection_request_event_t request;
	xcb_window_t owner;
	xcb_timestamp_t timestamp;
	int incr;
	int source_fd;
	int property_start;
	xcb_get_property_reply_t *property_reply;
	struct wl_event_source *property_source;
	int flush_property_on_delete;
	struct wl_array source_data;
	xcb_atom_t target;
	bool property_set;
};

struct wlr_xwm {
	struct wlr_xwayland *xwayland;
	struct wl_event_source *event_source;
	struct wlr_seat *seat;

	xcb_atom_t atoms[ATOM_LAST];
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_visualid_t visual_id;
	xcb_colormap_t colormap;
	xcb_render_pictformat_t render_format_id;
	xcb_cursor_t cursor;

	xcb_window_t selection_window;
	struct wlr_xwm_selection clipboard_selection;
	struct wlr_xwm_selection primary_selection;

	struct wlr_xwayland_surface *focus_surface;

	struct wl_list surfaces; // wlr_xwayland_surface::link
	struct wl_list unpaired_surfaces; // wlr_xwayland_surface::unpaired_link

	const xcb_query_extension_reply_t *xfixes;

	struct wl_listener compositor_surface_create;
	struct wl_listener seat_selection;
	struct wl_listener seat_primary_selection;
};

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland);

void xwm_destroy(struct wlr_xwm *xwm);

void xwm_set_cursor(struct wlr_xwm *xwm, const uint8_t *pixels, uint32_t stride,
	uint32_t width, uint32_t height, int32_t hotspot_x, int32_t hotspot_y);

int xwm_handle_selection_event(struct wlr_xwm *xwm, xcb_generic_event_t *event);

void xwm_selection_init(struct wlr_xwm *xwm);
void xwm_selection_finish(struct wlr_xwm *xwm);

void xwm_set_seat(struct wlr_xwm *xwm, struct wlr_seat *seat);

#endif
