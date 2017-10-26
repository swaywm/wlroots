#ifndef XWAYLAND_INTERNALS_H
#define XWAYLAND_INTERNALS_H
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
	ATOM_LAST,
};

extern const char *atom_map[ATOM_LAST];

enum net_wm_state_action {
	NET_WM_STATE_REMOVE = 0,
	NET_WM_STATE_ADD = 1,
	NET_WM_STATE_TOGGLE = 2,
};

struct wlr_xwm {
	struct wlr_xwayland *xwayland;
	struct wl_event_source *event_source;

	xcb_atom_t atoms[ATOM_LAST];
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t window;

	struct wlr_xwayland_surface *focus_surface;

	struct wl_list surfaces; // wlr_xwayland_surface::link
	struct wl_list unpaired_surfaces; // wlr_xwayland_surface::unpaired_link

	const xcb_query_extension_reply_t *xfixes;

	struct wl_listener compositor_surface_create;
};

void xwm_destroy(struct wlr_xwm *xwm);

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland);

#endif
