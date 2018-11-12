#ifndef XWAYLAND_XWM_H
#define XWAYLAND_XWM_H

#include <wayland-server-core.h>
#include <wlr/config.h>
#include <wlr/xwayland.h>
#include <xcb/render.h>
#if WLR_HAS_XCB_ICCCM
#include <xcb/xcb_icccm.h>
#endif
#if WLR_HAS_XCB_ERRORS
#include <xcb/xcb_errors.h>
#endif
#include "xwayland/selection.h"

/* This is in xcb/xcb_event.h, but pulling xcb-util just for a constant
 * others redefine anyway is meh
 */
#define XCB_EVENT_RESPONSE_TYPE_MASK (0x7f)

enum atom_name {
	WL_SURFACE_ID,
	WM_DELETE_WINDOW,
	WM_PROTOCOLS,
	WM_HINTS,
	WM_NORMAL_HINTS,
	WM_SIZE_HINTS,
	WM_WINDOW_ROLE,
	MOTIF_WM_HINTS,
	UTF8_STRING,
	WM_S0,
	NET_SUPPORTED,
	NET_WM_CM_S0,
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
	_NET_WM_STATE_MODAL,
	_NET_WM_STATE_FULLSCREEN,
	_NET_WM_STATE_MAXIMIZED_VERT,
	_NET_WM_STATE_MAXIMIZED_HORZ,
	_NET_WM_PING,
	WM_STATE,
	CLIPBOARD,
	PRIMARY,
	WL_SELECTION,
	TARGETS,
	CLIPBOARD_MANAGER,
	INCR,
	TEXT,
	TIMESTAMP,
	DELETE,
	NET_WM_WINDOW_TYPE_NORMAL,
	NET_WM_WINDOW_TYPE_UTILITY,
	NET_WM_WINDOW_TYPE_TOOLTIP,
	NET_WM_WINDOW_TYPE_DND,
	NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
	NET_WM_WINDOW_TYPE_POPUP_MENU,
	NET_WM_WINDOW_TYPE_COMBO,
	NET_WM_WINDOW_TYPE_MENU,
	NET_WM_WINDOW_TYPE_NOTIFICATION,
	NET_WM_WINDOW_TYPE_SPLASH,
	DND_SELECTION,
	DND_AWARE,
	DND_STATUS,
	DND_POSITION,
	DND_ENTER,
	DND_LEAVE,
	DND_DROP,
	DND_FINISHED,
	DND_PROXY,
	DND_TYPE_LIST,
	DND_ACTION_MOVE,
	DND_ACTION_COPY,
	DND_ACTION_ASK,
	DND_ACTION_PRIVATE,
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
	struct wlr_seat *seat;
	uint32_t ping_timeout;

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

	xcb_window_t dnd_window;
	struct wlr_xwm_selection dnd_selection;

	struct wlr_xwayland_surface *focus_surface;

	struct wl_list surfaces; // wlr_xwayland_surface::link
	struct wl_list unpaired_surfaces; // wlr_xwayland_surface::unpaired_link

	struct wlr_drag *drag;
	struct wlr_xwayland_surface *drag_focus;

	const xcb_query_extension_reply_t *xfixes;
#if WLR_HAS_XCB_ERRORS
	xcb_errors_context_t *errors_context;
#endif

	struct wl_listener compositor_new_surface;
	struct wl_listener compositor_destroy;
	struct wl_listener seat_selection;
	struct wl_listener seat_primary_selection;
	struct wl_listener seat_start_drag;
	struct wl_listener seat_drag_focus;
	struct wl_listener seat_drag_motion;
	struct wl_listener seat_drag_drop;
	struct wl_listener seat_drag_destroy;
	struct wl_listener seat_drag_source_destroy;
};

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland);

void xwm_destroy(struct wlr_xwm *xwm);

void xwm_set_cursor(struct wlr_xwm *xwm, const uint8_t *pixels, uint32_t stride,
	uint32_t width, uint32_t height, int32_t hotspot_x, int32_t hotspot_y);

int xwm_handle_selection_event(struct wlr_xwm *xwm, xcb_generic_event_t *event);
int xwm_handle_selection_client_message(struct wlr_xwm *xwm,
	xcb_client_message_event_t *ev);

void xwm_set_seat(struct wlr_xwm *xwm, struct wlr_seat *seat);

char *xwm_get_atom_name(struct wlr_xwm *xwm, xcb_atom_t atom);
bool xwm_atoms_contains(struct wlr_xwm *xwm, xcb_atom_t *atoms,
	size_t num_atoms, enum atom_name needle);

#endif
