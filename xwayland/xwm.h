#ifndef XWAYLAND_INTERNALS_H
#define XWAYLAND_INTERNALS_H
#include <wayland-server-core.h>
#include <wlr/xwayland.h>

/* wlc's atom list:
   WL_SURFACE_ID,
   WM_DELETE_WINDOW,
   WM_TAKE_FOCUS,
   WM_PROTOCOLS,
   WM_NORMAL_HINTS,
   MOTIF_WM_HINTS,
   TEXT,
   UTF8_STRING,
   CLIPBOARD,
   CLIPBOARD_MANAGER,
   TARGETS,
   PRIMARY,
   WM_S0,
   STRING,
   WLC_SELECTION,
   NET_WM_S0,
   NET_WM_PID,
   NET_WM_NAME,
   NET_WM_STATE,
   NET_WM_STATE_FULLSCREEN,
   NET_WM_STATE_MODAL,
   NET_WM_STATE_ABOVE,
   NET_SUPPORTED,
   NET_SUPPORTING_WM_CHECK,
   NET_WM_WINDOW_TYPE,
   NET_WM_WINDOW_TYPE_DESKTOP,
   NET_WM_WINDOW_TYPE_DOCK,
   NET_WM_WINDOW_TYPE_TOOLBAR,
   NET_WM_WINDOW_TYPE_MENU,
   NET_WM_WINDOW_TYPE_UTILITY,
   NET_WM_WINDOW_TYPE_SPLASH,
   NET_WM_WINDOW_TYPE_DIALOG,
   NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
   NET_WM_WINDOW_TYPE_POPUP_MENU,
   NET_WM_WINDOW_TYPE_TOOLTIP,
   NET_WM_WINDOW_TYPE_NOTIFICATION,
   NET_WM_WINDOW_TYPE_COMBO,
   NET_WM_WINDOW_TYPE_DND,
   NET_WM_WINDOW_TYPE_NORMAL,
 */

enum atom_name {
	WL_SURFACE_ID,
	WM_PROTOCOLS,
	UTF8_STRING,
	WM_S0,
	NET_SUPPORTED,
	NET_WM_S0,
	NET_WM_PID,
	NET_WM_NAME,
	NET_WM_STATE,
	NET_WM_WINDOW_TYPE,
	WM_TAKE_FOCUS,
	ATOM_LAST,
};

extern const char *atom_map[ATOM_LAST];

enum net_wm_state_action {
	NET_WM_STATE_REMOVE = 0,
	NET_WM_STATE_ADD    = 1,
	NET_WM_STATE_TOGGLE = 2,
};

struct wlr_xwm {
	struct wlr_xwayland *xwayland;
	struct wl_event_source *event_source;
	struct wl_listener surface_create_listener;

	xcb_atom_t atoms[ATOM_LAST];
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t window;

	struct wl_list new_surfaces;
	struct wl_list unpaired_surfaces;
};

void xwm_destroy(struct wlr_xwm *xwm);
struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland);

#endif
