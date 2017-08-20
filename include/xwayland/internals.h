#ifndef XWAYLAND_INTERNALS_H
#define XWAYLAND_INTERNALS_H
#include <xcb/xcb.h>
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
	WM_S0,
	NET_SUPPORTED,
	NET_WM_S0,
	NET_WM_STATE,
	ATOM_LAST
};

static const char * const atom_map[ATOM_LAST] = {
	"WL_SURFACE_ID",
	"WM_PROTOCOLS",
	"WM_S0",
	"_NET_SUPPORTED",
	"_NET_WM_S0",
	"_NET_WM_STATE",
};


struct wlr_xwm {
	struct wlr_xwayland *xwayland;
	struct wl_event_source *event_source;
	struct wl_listener surface_listener;

	xcb_atom_t atoms[ATOM_LAST];
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t window;
};

void unlink_sockets(int display);
int open_display_sockets(int socks[2]);

void xwm_destroy(struct wlr_xwm *xwm);
struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland);

#endif
