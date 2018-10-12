#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/config.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland.h>
#include <xcb/composite.h>
#include <xcb/render.h>
#include <xcb/xfixes.h>
#include "util/signal.h"
#include "xwayland/xwm.h"

const char *atom_map[ATOM_LAST] = {
	"WL_SURFACE_ID",
	"WM_DELETE_WINDOW",
	"WM_PROTOCOLS",
	"WM_HINTS",
	"WM_NORMAL_HINTS",
	"WM_SIZE_HINTS",
	"WM_WINDOW_ROLE",
	"_MOTIF_WM_HINTS",
	"UTF8_STRING",
	"WM_S0",
	"_NET_SUPPORTED",
	"_NET_WM_S0",
	"_NET_WM_PID",
	"_NET_WM_NAME",
	"_NET_WM_STATE",
	"_NET_WM_WINDOW_TYPE",
	"WM_TAKE_FOCUS",
	"WINDOW",
	"_NET_ACTIVE_WINDOW",
	"_NET_WM_MOVERESIZE",
	"_NET_WM_NAME",
	"_NET_SUPPORTING_WM_CHECK",
	"_NET_WM_STATE_MODAL",
	"_NET_WM_STATE_FULLSCREEN",
	"_NET_WM_STATE_MAXIMIZED_VERT",
	"_NET_WM_STATE_MAXIMIZED_HORZ",
	"_NET_WM_PING",
	"WM_STATE",
	"CLIPBOARD",
	"PRIMARY",
	"_WL_SELECTION",
	"TARGETS",
	"CLIPBOARD_MANAGER",
	"INCR",
	"TEXT",
	"TIMESTAMP",
	"DELETE",
	"_NET_WM_WINDOW_TYPE_NORMAL",
	"_NET_WM_WINDOW_TYPE_UTILITY",
	"_NET_WM_WINDOW_TYPE_TOOLTIP",
	"_NET_WM_WINDOW_TYPE_DND",
	"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
	"_NET_WM_WINDOW_TYPE_POPUP_MENU",
	"_NET_WM_WINDOW_TYPE_COMBO",
	"_NET_WM_WINDOW_TYPE_MENU",
	"_NET_WM_WINDOW_TYPE_NOTIFICATION",
	"_NET_WM_WINDOW_TYPE_SPLASH",
	"XdndSelection",
	"XdndAware",
	"XdndStatus",
	"XdndPosition",
	"XdndEnter",
	"XdndLeave",
	"XdndDrop",
	"XdndFinished",
	"XdndProxy",
	"XdndTypeList",
	"XdndActionMove",
	"XdndActionCopy",
	"XdndActionAsk",
	"XdndActionPrivate",
};

static const struct wlr_surface_role xwayland_surface_role;

bool wlr_surface_is_xwayland_surface(struct wlr_surface *surface) {
	return surface->role == &xwayland_surface_role;
}

struct wlr_xwayland_surface *wlr_xwayland_surface_from_wlr_surface(
		struct wlr_surface *surface) {
	assert(wlr_surface_is_xwayland_surface(surface));
	return (struct wlr_xwayland_surface *)surface->role_data;
}

// TODO: replace this with hash table?
static struct wlr_xwayland_surface *lookup_surface(struct wlr_xwm *xwm,
		xcb_window_t window_id) {
	struct wlr_xwayland_surface *surface;
	wl_list_for_each(surface, &xwm->surfaces, link) {
		if (surface->window_id == window_id) {
			return surface;
		}
	}
	return NULL;
}

static int xwayland_surface_handle_ping_timeout(void *data) {
	struct wlr_xwayland_surface *surface = data;

	wlr_signal_emit_safe(&surface->events.ping_timeout, surface);
	surface->pinging = false;
	return 1;
}

static struct wlr_xwayland_surface *xwayland_surface_create(
		struct wlr_xwm *xwm, xcb_window_t window_id, int16_t x, int16_t y,
		uint16_t width, uint16_t height, bool override_redirect) {
	struct wlr_xwayland_surface *surface =
		calloc(1, sizeof(struct wlr_xwayland_surface));
	if (!surface) {
		wlr_log(WLR_ERROR, "Could not allocate wlr xwayland surface");
		return NULL;
	}

	xcb_get_geometry_cookie_t geometry_cookie =
		xcb_get_geometry(xwm->xcb_conn, window_id);

	uint32_t values[1];
	values[0] =
		XCB_EVENT_MASK_FOCUS_CHANGE |
		XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes(xwm->xcb_conn, window_id,
		XCB_CW_EVENT_MASK, values);

	surface->xwm = xwm;
	surface->window_id = window_id;
	surface->x = x;
	surface->y = y;
	surface->width = width;
	surface->height = height;
	surface->override_redirect = override_redirect;
	wl_list_insert(&xwm->surfaces, &surface->link);
	wl_list_init(&surface->children);
	wl_list_init(&surface->parent_link);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.request_configure);
	wl_signal_init(&surface->events.request_move);
	wl_signal_init(&surface->events.request_resize);
	wl_signal_init(&surface->events.request_maximize);
	wl_signal_init(&surface->events.request_fullscreen);
	wl_signal_init(&surface->events.request_activate);
	wl_signal_init(&surface->events.map);
	wl_signal_init(&surface->events.unmap);
	wl_signal_init(&surface->events.set_class);
	wl_signal_init(&surface->events.set_role);
	wl_signal_init(&surface->events.set_title);
	wl_signal_init(&surface->events.set_parent);
	wl_signal_init(&surface->events.set_pid);
	wl_signal_init(&surface->events.set_window_type);
	wl_signal_init(&surface->events.set_hints);
	wl_signal_init(&surface->events.set_decorations);
	wl_signal_init(&surface->events.set_override_redirect);
	wl_signal_init(&surface->events.ping_timeout);

	xcb_get_geometry_reply_t *geometry_reply =
		xcb_get_geometry_reply(xwm->xcb_conn, geometry_cookie, NULL);
	if (geometry_reply != NULL) {
		surface->has_alpha = geometry_reply->depth == 32;
	}
	free(geometry_reply);

	struct wl_display *display = xwm->xwayland->wl_display;
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	surface->ping_timer = wl_event_loop_add_timer(loop,
		xwayland_surface_handle_ping_timeout, surface);
	if (surface->ping_timer == NULL) {
		free(surface);
		wlr_log(WLR_ERROR, "Could not add timer to event loop");
		return NULL;
	}

	wlr_signal_emit_safe(&xwm->xwayland->events.new_surface, surface);

	return surface;
}

static void xwm_set_net_active_window(struct wlr_xwm *xwm,
		xcb_window_t window) {
	xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE,
			xwm->screen->root, xwm->atoms[_NET_ACTIVE_WINDOW],
			xwm->atoms[WINDOW], 32, 1, &window);
}

static void xwm_send_wm_message(struct wlr_xwayland_surface *surface,
		xcb_client_message_data_t *data, uint32_t event_mask) {
	struct wlr_xwm *xwm = surface->xwm;

	xcb_client_message_event_t event = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = surface->window_id,
		.type = xwm->atoms[WM_PROTOCOLS],
		.data = *data,
	};

	xcb_send_event(xwm->xcb_conn,
		0, // propagate
		surface->window_id,
		event_mask,
		(const char *)&event);
	xcb_flush(xwm->xcb_conn);
}

static void xwm_send_focus_window(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface) {
	if (!xsurface) {
		xcb_set_input_focus_checked(xwm->xcb_conn,
			XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_NONE, XCB_CURRENT_TIME);
		return;
	}

	if (xsurface->override_redirect) {
		return;
	}

	xcb_client_message_data_t message_data = { 0 };
	message_data.data32[0] = xwm->atoms[WM_TAKE_FOCUS];
	message_data.data32[1] = XCB_TIME_CURRENT_TIME;

	if (xsurface->hints && !xsurface->hints->input) {
		// if the surface doesn't allow the focus request, we will send him
		// only the take focus event. It will get the focus by itself.
		xwm_send_wm_message(xsurface, &message_data, XCB_EVENT_MASK_NO_EVENT);
	} else {
		xwm_send_wm_message(xsurface, &message_data, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT);

		xcb_set_input_focus(xwm->xcb_conn, XCB_INPUT_FOCUS_POINTER_ROOT,
			xsurface->window_id, XCB_CURRENT_TIME);
	}

	uint32_t values[1];
	values[0] = XCB_STACK_MODE_ABOVE;
	xcb_configure_window(xwm->xcb_conn, xsurface->window_id,
		XCB_CONFIG_WINDOW_STACK_MODE, values);
}

static void xwm_surface_activate(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface) {
	if (xwm->focus_surface == xsurface ||
			(xsurface && xsurface->override_redirect)) {
		return;
	}

	if (xsurface) {
		xwm_set_net_active_window(xwm, xsurface->window_id);
	} else {
		xwm_set_net_active_window(xwm, XCB_WINDOW_NONE);
	}

	xwm_send_focus_window(xwm, xsurface);

	xwm->focus_surface = xsurface;

	xcb_flush(xwm->xcb_conn);
}

static void xsurface_set_net_wm_state(struct wlr_xwayland_surface *xsurface) {
	struct wlr_xwm *xwm = xsurface->xwm;
	uint32_t property[3];
	int i;

	i = 0;
	if (xsurface->modal) {
		property[i++] = xwm->atoms[_NET_WM_STATE_MODAL];
	}
	if (xsurface->fullscreen) {
		property[i++] = xwm->atoms[_NET_WM_STATE_FULLSCREEN];
	}
	if (xsurface->maximized_vert) {
		property[i++] = xwm->atoms[_NET_WM_STATE_MAXIMIZED_VERT];
	}
	if (xsurface->maximized_horz) {
		property[i++] = xwm->atoms[_NET_WM_STATE_MAXIMIZED_HORZ];
	}

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xsurface->window_id,
		xwm->atoms[NET_WM_STATE],
		XCB_ATOM_ATOM,
		32, // format
		i, property);
}

static void xsurface_unmap(struct wlr_xwayland_surface *surface);

static void xwayland_surface_destroy(
		struct wlr_xwayland_surface *xsurface) {
	xsurface_unmap(xsurface);

	wlr_signal_emit_safe(&xsurface->events.destroy, xsurface);

	if (xsurface == xsurface->xwm->focus_surface) {
		xwm_surface_activate(xsurface->xwm, NULL);
	}

	wl_list_remove(&xsurface->link);
	wl_list_remove(&xsurface->parent_link);

	struct wlr_xwayland_surface *child, *next;
	wl_list_for_each_safe(child, next, &xsurface->children, parent_link) {
		wl_list_remove(&child->parent_link);
		wl_list_init(&child->parent_link);
	}

	if (xsurface->surface_id) {
		wl_list_remove(&xsurface->unpaired_link);
	}

	if (xsurface->surface) {
		wl_list_remove(&xsurface->surface_destroy.link);
		xsurface->surface->role_data = NULL;
	}

	wl_event_source_remove(xsurface->ping_timer);

	free(xsurface->title);
	free(xsurface->class);
	free(xsurface->instance);
	free(xsurface->role);
	free(xsurface->window_type);
	free(xsurface->protocols);
	free(xsurface->hints);
	free(xsurface->size_hints);
	free(xsurface);
}

static void read_surface_class(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_STRING &&
			reply->type != xwm->atoms[UTF8_STRING]) {
		return;
	}

	size_t len = xcb_get_property_value_length(reply);
	char *class = xcb_get_property_value(reply);

	// Unpack two sequentially stored strings: instance, class
	size_t instance_len = strnlen(class, len);
	free(surface->instance);
	if (len > 0 && instance_len < len) {
		surface->instance = strndup(class, instance_len);
		class += instance_len + 1;
	} else {
		surface->instance = NULL;
	}
	free(surface->class);
	if (len > 0) {
		surface->class = strndup(class, len);
	} else {
		surface->class = NULL;
	}

	wlr_log(WLR_DEBUG, "XCB_ATOM_WM_CLASS: %s %s", surface->instance,
		surface->class);
	wlr_signal_emit_safe(&surface->events.set_class, surface);
}

static void read_surface_role(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_STRING &&
			reply->type != xwm->atoms[UTF8_STRING]) {
		return;
	}

	size_t len = xcb_get_property_value_length(reply);
	char *role = xcb_get_property_value(reply);

	free(xsurface->role);
	if (len > 0) {
		xsurface->role = strndup(role, len);
	} else {
		xsurface->role = NULL;
	}

	wlr_log(WLR_DEBUG, "XCB_ATOM_WM_WINDOW_ROLE: %s", xsurface->role);
	wlr_signal_emit_safe(&xsurface->events.set_role, xsurface);
}

static void read_surface_title(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_STRING &&
			reply->type != xwm->atoms[UTF8_STRING]) {
		return;
	}

	bool is_utf8 = reply->type == xwm->atoms[UTF8_STRING];
	if (!is_utf8 && xsurface->has_utf8_title) {
		return;
	}

	size_t len = xcb_get_property_value_length(reply);
	char *title = xcb_get_property_value(reply);

	free(xsurface->title);
	if (len > 0) {
		xsurface->title = strndup(title, len);
	} else {
		xsurface->title = NULL;
	}
	xsurface->has_utf8_title = is_utf8;

	wlr_log(WLR_DEBUG, "XCB_ATOM_WM_NAME: %s", xsurface->title);
	wlr_signal_emit_safe(&xsurface->events.set_title, xsurface);
}

static void read_surface_parent(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_WINDOW) {
		return;
	}

	xcb_window_t *xid = xcb_get_property_value(reply);
	if (xid != NULL) {
		xsurface->parent = lookup_surface(xwm, *xid);
	} else {
		xsurface->parent = NULL;
	}

	wl_list_remove(&xsurface->parent_link);
	if (xsurface->parent != NULL) {
		wl_list_insert(&xsurface->parent->children, &xsurface->parent_link);
	} else {
		wl_list_init(&xsurface->parent_link);
	}

	wlr_log(WLR_DEBUG, "XCB_ATOM_WM_TRANSIENT_FOR: %p", xsurface->parent);
	wlr_signal_emit_safe(&xsurface->events.set_parent, xsurface);
}

static void read_surface_pid(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_CARDINAL) {
		return;
	}

	pid_t *pid = xcb_get_property_value(reply);
	xsurface->pid = *pid;
	wlr_log(WLR_DEBUG, "NET_WM_PID %d", xsurface->pid);
	wlr_signal_emit_safe(&xsurface->events.set_pid, xsurface);
}

static void read_surface_window_type(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_ATOM) {
		return;
	}

	xcb_atom_t *atoms = xcb_get_property_value(reply);
	size_t atoms_len = reply->value_len;
	size_t atoms_size = sizeof(xcb_atom_t) * atoms_len;

	free(xsurface->window_type);
	xsurface->window_type = malloc(atoms_size);
	if (xsurface->window_type == NULL) {
		return;
	}
	memcpy(xsurface->window_type, atoms, atoms_size);
	xsurface->window_type_len = atoms_len;

	wlr_log(WLR_DEBUG, "NET_WM_WINDOW_TYPE (%zu)", atoms_len);
	wlr_signal_emit_safe(&xsurface->events.set_window_type, xsurface);
}

static void read_surface_protocols(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_ATOM) {
		return;
	}

	xcb_atom_t *atoms = xcb_get_property_value(reply);
	size_t atoms_len = reply->value_len;
	size_t atoms_size = sizeof(xcb_atom_t) * atoms_len;

	free(xsurface->protocols);
	xsurface->protocols = malloc(atoms_size);
	if (xsurface->protocols == NULL) {
		return;
	}
	memcpy(xsurface->protocols, atoms, atoms_size);
	xsurface->protocols_len = atoms_len;

	wlr_log(WLR_DEBUG, "WM_PROTOCOLS (%zu)", atoms_len);
}

#ifdef WLR_HAS_XCB_ICCCM
static void read_surface_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	// According to the docs, reply->type == xwm->atoms[WM_HINTS]
	// In practice, reply->type == XCB_ATOM_ATOM
	if (reply->value_len == 0) {
		return;
	}

	xcb_icccm_wm_hints_t hints;
	xcb_icccm_get_wm_hints_from_reply(&hints, reply);

	free(xsurface->hints);
	xsurface->hints = calloc(1, sizeof(struct wlr_xwayland_surface_hints));
	if (xsurface->hints == NULL) {
		return;
	}
	memcpy(xsurface->hints, &hints, sizeof(struct wlr_xwayland_surface_hints));
	xsurface->hints_urgency = xcb_icccm_wm_hints_get_urgency(&hints);

	if (!(xsurface->hints->flags & XCB_ICCCM_WM_HINT_INPUT)) {
		// The client didn't specify whether it wants input.
		// Assume it does.
		xsurface->hints->input = true;
	}

	wlr_log(WLR_DEBUG, "WM_HINTS (%d)", reply->value_len);
	wlr_signal_emit_safe(&xsurface->events.set_hints, xsurface);
}
#else
static void read_surface_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	// Do nothing
}
#endif

#ifdef WLR_HAS_XCB_ICCCM
static void read_surface_normal_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != xwm->atoms[WM_SIZE_HINTS] || reply->value_len == 0) {
		return;
	}

	xcb_size_hints_t size_hints;
	xcb_icccm_get_wm_size_hints_from_reply(&size_hints, reply);

	free(xsurface->size_hints);
	xsurface->size_hints =
		calloc(1, sizeof(struct wlr_xwayland_surface_size_hints));
	if (xsurface->size_hints == NULL) {
		return;
	}
	memcpy(xsurface->size_hints, &size_hints,
		sizeof(struct wlr_xwayland_surface_size_hints));

	wlr_log(WLR_DEBUG, "WM_NORMAL_HINTS (%d)", reply->value_len);
}
#else
static void read_surface_normal_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	// Do nothing
}
#endif


#define MWM_HINTS_FLAGS_FIELD 0
#define MWM_HINTS_DECORATIONS_FIELD 2

#define MWM_HINTS_DECORATIONS (1 << 1)

#define MWM_DECOR_ALL (1 << 0)
#define MWM_DECOR_BORDER (1 << 1)
#define MWM_DECOR_TITLE (1 << 3)

static void read_surface_motif_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->value_len < 5) {
		return;
	}

	uint32_t *motif_hints = xcb_get_property_value(reply);
	if (motif_hints[MWM_HINTS_FLAGS_FIELD] & MWM_HINTS_DECORATIONS) {
		xsurface->decorations = WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
		uint32_t decorations = motif_hints[MWM_HINTS_DECORATIONS_FIELD];
		if ((decorations & MWM_DECOR_ALL) == 0) {
			if ((decorations & MWM_DECOR_BORDER) == 0) {
				xsurface->decorations |=
					WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER;
			}
			if ((decorations & MWM_DECOR_TITLE) == 0) {
				xsurface->decorations |=
					WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE;
			}
		}
		wlr_signal_emit_safe(&xsurface->events.set_decorations, xsurface);
	}

	wlr_log(WLR_DEBUG, "MOTIF_WM_HINTS (%d)", reply->value_len);
}

static void read_surface_net_wm_state(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	xsurface->fullscreen = 0;
	xcb_atom_t *atom = xcb_get_property_value(reply);
	for (uint32_t i = 0; i < reply->value_len; i++) {
		if (atom[i] == xwm->atoms[_NET_WM_STATE_MODAL]) {
			xsurface->modal = true;
		} else if (atom[i] == xwm->atoms[_NET_WM_STATE_FULLSCREEN]) {
			xsurface->fullscreen = true;
		} else if (atom[i] == xwm->atoms[_NET_WM_STATE_MAXIMIZED_VERT]) {
			xsurface->maximized_vert = true;
		} else if (atom[i] == xwm->atoms[_NET_WM_STATE_MAXIMIZED_HORZ]) {
			xsurface->maximized_horz = true;
		}
	}
}

char *xwm_get_atom_name(struct wlr_xwm *xwm, xcb_atom_t atom) {
	xcb_get_atom_name_cookie_t name_cookie =
		xcb_get_atom_name(xwm->xcb_conn, atom);
	xcb_get_atom_name_reply_t *name_reply =
		xcb_get_atom_name_reply(xwm->xcb_conn, name_cookie, NULL);
	if (name_reply == NULL) {
		return NULL;
	}
	size_t len = xcb_get_atom_name_name_length(name_reply);
	char *buf = xcb_get_atom_name_name(name_reply); // not a C string
	char *name = strndup(buf, len);
	free(name_reply);
	return name;
}

static void read_surface_property(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface, xcb_atom_t property) {
	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn, 0,
		xsurface->window_id, property, XCB_ATOM_ANY, 0, 2048);
	xcb_get_property_reply_t *reply = xcb_get_property_reply(xwm->xcb_conn,
		cookie, NULL);
	if (reply == NULL) {
		return;
	}

	if (property == XCB_ATOM_WM_CLASS) {
		read_surface_class(xwm, xsurface, reply);
	} else if (property == XCB_ATOM_WM_NAME ||
			property == xwm->atoms[NET_WM_NAME]) {
		read_surface_title(xwm, xsurface, reply);
	} else if (property == XCB_ATOM_WM_TRANSIENT_FOR) {
		read_surface_parent(xwm, xsurface, reply);
	} else if (property == xwm->atoms[NET_WM_PID]) {
		read_surface_pid(xwm, xsurface, reply);
	} else if (property == xwm->atoms[NET_WM_WINDOW_TYPE]) {
		read_surface_window_type(xwm, xsurface, reply);
	} else if (property == xwm->atoms[WM_PROTOCOLS]) {
		read_surface_protocols(xwm, xsurface, reply);
	} else if (property == xwm->atoms[NET_WM_STATE]) {
		read_surface_net_wm_state(xwm, xsurface, reply);
	} else if (property == xwm->atoms[WM_HINTS]) {
		read_surface_hints(xwm, xsurface, reply);
	} else if (property == xwm->atoms[WM_NORMAL_HINTS]) {
		read_surface_normal_hints(xwm, xsurface, reply);
	} else if (property == xwm->atoms[MOTIF_WM_HINTS]) {
		read_surface_motif_hints(xwm, xsurface, reply);
	} else if (property == xwm->atoms[WM_WINDOW_ROLE]) {
		read_surface_role(xwm, xsurface, reply);
	} else {
		char *prop_name = xwm_get_atom_name(xwm, property);
		wlr_log(WLR_DEBUG, "unhandled X11 property %u (%s) for window %u",
			property, prop_name, xsurface->window_id);
		free(prop_name);
	}

	free(reply);
}

static void xwayland_surface_role_commit(struct wlr_surface *wlr_surface) {
	assert(wlr_surface->role == &xwayland_surface_role);
	struct wlr_xwayland_surface *surface = wlr_surface->role_data;
	if (surface == NULL) {
		return;
	}

	if (!surface->mapped && wlr_surface_has_buffer(surface->surface)) {
		wlr_signal_emit_safe(&surface->events.map, surface);
		surface->mapped = true;
	}
}

static void xwayland_surface_role_precommit(struct wlr_surface *wlr_surface) {
	assert(wlr_surface->role == &xwayland_surface_role);
	struct wlr_xwayland_surface *surface = wlr_surface->role_data;
	if (surface == NULL) {
		return;
	}

	if (wlr_surface->pending.committed & WLR_SURFACE_STATE_BUFFER &&
			wlr_surface->pending.buffer_resource == NULL) {
		// This is a NULL commit
		if (surface->mapped) {
			wlr_signal_emit_safe(&surface->events.unmap, surface);
			surface->mapped = false;
		}
	}
}

static const struct wlr_surface_role xwayland_surface_role = {
	.name = "wlr_xwayland_surface",
	.commit = xwayland_surface_role_commit,
	.precommit = xwayland_surface_role_precommit,
};

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_surface *surface =
		wl_container_of(listener, surface, surface_destroy);
	xsurface_unmap(surface);
}

static void xwm_map_shell_surface(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface, struct wlr_surface *surface) {
	if (!wlr_surface_set_role(surface, &xwayland_surface_role, xsurface,
			NULL, 0)) {
		wlr_log(WLR_ERROR, "Failed to set xwayland surface role");
		return;
	}

	xsurface->surface = surface;

	// read all surface properties
	const xcb_atom_t props[] = {
		XCB_ATOM_WM_CLASS,
		XCB_ATOM_WM_NAME,
		XCB_ATOM_WM_TRANSIENT_FOR,
		xwm->atoms[WM_PROTOCOLS],
		xwm->atoms[WM_HINTS],
		xwm->atoms[WM_NORMAL_HINTS],
		xwm->atoms[MOTIF_WM_HINTS],
		xwm->atoms[NET_WM_STATE],
		xwm->atoms[NET_WM_WINDOW_TYPE],
		xwm->atoms[NET_WM_NAME],
		xwm->atoms[NET_WM_PID],
	};
	for (size_t i = 0; i < sizeof(props)/sizeof(xcb_atom_t); i++) {
		read_surface_property(xwm, xsurface, props[i]);
	}

	xsurface->surface_destroy.notify = handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &xsurface->surface_destroy);
}

static void xsurface_unmap(struct wlr_xwayland_surface *surface) {
	if (surface->mapped) {
		surface->mapped = false;
		wlr_signal_emit_safe(&surface->events.unmap, surface);
	}

	if (surface->surface_id) {
		// Make sure we're not on the unpaired surface list or we
		// could be assigned a surface during surface creation that
		// was mapped before this unmap request.
		wl_list_remove(&surface->unpaired_link);
		surface->surface_id = 0;
	}

	if (surface->surface) {
		wl_list_remove(&surface->surface_destroy.link);
		surface->surface->role_data = NULL;
		surface->surface = NULL;
	}
}

static void xwm_handle_create_notify(struct wlr_xwm *xwm,
		xcb_create_notify_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_CREATE_NOTIFY (%u)", ev->window);

	if (ev->window == xwm->window ||
			ev->window == xwm->selection_window ||
			ev->window == xwm->dnd_window) {
		return;
	}

	xwayland_surface_create(xwm, ev->window, ev->x, ev->y,
		ev->width, ev->height, ev->override_redirect);
}

static void xwm_handle_destroy_notify(struct wlr_xwm *xwm,
		xcb_destroy_notify_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_DESTROY_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		return;
	}
	xwayland_surface_destroy(xsurface);
}

static void xwm_handle_configure_request(struct wlr_xwm *xwm,
		xcb_configure_request_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_CONFIGURE_REQUEST (%u) [%ux%u+%d,%d]", ev->window,
		ev->width, ev->height, ev->x, ev->y);
	struct wlr_xwayland_surface *surface = lookup_surface(xwm, ev->window);
	if (surface == NULL) {
		return;
	}

	// TODO: handle ev->{parent,sibling}?

	struct wlr_xwayland_surface_configure_event wlr_event = {
		.surface = surface,
		.x = ev->x,
		.y = ev->y,
		.width = ev->width,
		.height = ev->height,
	};

	wlr_signal_emit_safe(&surface->events.request_configure, &wlr_event);
}

static void xwm_handle_configure_notify(struct wlr_xwm *xwm,
		xcb_configure_notify_event_t *ev) {
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (!xsurface) {
		return;
	}

	xsurface->x = ev->x;
	xsurface->y = ev->y;
	xsurface->width = ev->width;
	xsurface->height = ev->height;

	if (xsurface->override_redirect != ev->override_redirect) {
		xsurface->override_redirect = ev->override_redirect;
		wlr_signal_emit_safe(&xsurface->events.set_override_redirect, xsurface);
	}
}

#define ICCCM_WITHDRAWN_STATE	0
#define ICCCM_NORMAL_STATE	1
#define ICCCM_ICONIC_STATE	3

static void xsurface_set_wm_state(struct wlr_xwayland_surface *xsurface,
		int32_t state) {
	struct wlr_xwm *xwm = xsurface->xwm;
	uint32_t property[2];

	property[0] = state;
	property[1] = XCB_WINDOW_NONE;

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xsurface->window_id,
		xwm->atoms[WM_STATE],
		xwm->atoms[WM_STATE],
		32, // format
		2, property);
}

static void xwm_handle_map_request(struct wlr_xwm *xwm,
		xcb_map_request_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_MAP_REQUEST (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (!xsurface) {
		return;
	}

	xsurface_set_wm_state(xsurface, ICCCM_NORMAL_STATE);
	xsurface_set_net_wm_state(xsurface);
	xcb_map_window(xwm->xcb_conn, ev->window);
}

static void xwm_handle_map_notify(struct wlr_xwm *xwm,
		xcb_map_notify_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_MAP_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (!xsurface) {
		return;
	}

	if (xsurface->override_redirect != ev->override_redirect) {
		xsurface->override_redirect = ev->override_redirect;
		wlr_signal_emit_safe(&xsurface->events.set_override_redirect, xsurface);
	}
}

static void xwm_handle_unmap_notify(struct wlr_xwm *xwm,
		xcb_unmap_notify_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_UNMAP_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		return;
	}

	xsurface_unmap(xsurface);
	xsurface_set_wm_state(xsurface, ICCCM_WITHDRAWN_STATE);
}

static void xwm_handle_property_notify(struct wlr_xwm *xwm,
		xcb_property_notify_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_PROPERTY_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		return;
	}

	read_surface_property(xwm, xsurface, ev->atom);
}

static void xwm_handle_surface_id_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		wlr_log(WLR_DEBUG,
			"client message WL_SURFACE_ID but no new window %u ?",
			ev->window);
		return;
	}
	/* Check if we got notified after wayland surface create event */
	uint32_t id = ev->data.data32[0];
	struct wl_resource *resource =
		wl_client_get_object(xwm->xwayland->client, id);
	if (resource) {
		struct wlr_surface *surface = wlr_surface_from_resource(resource);
		xsurface->surface_id = 0;
		xwm_map_shell_surface(xwm, xsurface, surface);
	} else {
		xsurface->surface_id = id;
		wl_list_insert(&xwm->unpaired_surfaces, &xsurface->unpaired_link);
	}
}

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8  // movement only
#define _NET_WM_MOVERESIZE_SIZE_KEYBOARD     9  // size via keyboard
#define _NET_WM_MOVERESIZE_MOVE_KEYBOARD    10  // move via keyboard
#define _NET_WM_MOVERESIZE_CANCEL           11  // cancel operation

static enum wlr_edges net_wm_edges_to_wlr(uint32_t net_wm_edges) {
	enum wlr_edges edges = WLR_EDGE_NONE;

	switch(net_wm_edges) {
		case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
			edges = WLR_EDGE_TOP | WLR_EDGE_LEFT;
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOP:
			edges = WLR_EDGE_TOP;
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
			edges = WLR_EDGE_TOP | WLR_EDGE_RIGHT;
			break;
		case _NET_WM_MOVERESIZE_SIZE_RIGHT:
			edges = WLR_EDGE_RIGHT;
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
			edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
			edges = WLR_EDGE_BOTTOM;
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
			edges = WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
			break;
		case _NET_WM_MOVERESIZE_SIZE_LEFT:
			edges = WLR_EDGE_LEFT;
			break;
		default:
			break;
	}

	return edges;
}

static void xwm_handle_net_wm_moveresize_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (!xsurface) {
		return;
	}

	// TODO: we should probably add input or seat info to this but we would just
	// be guessing
	struct wlr_xwayland_resize_event resize_event;
	struct wlr_xwayland_move_event move_event;

	int detail = ev->data.data32[2];
	switch (detail) {
	case _NET_WM_MOVERESIZE_MOVE:
		move_event.surface = xsurface;
		wlr_signal_emit_safe(&xsurface->events.request_move, &move_event);
		break;
	case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
	case _NET_WM_MOVERESIZE_SIZE_TOP:
	case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
	case _NET_WM_MOVERESIZE_SIZE_RIGHT:
	case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
	case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
	case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
	case _NET_WM_MOVERESIZE_SIZE_LEFT:
		resize_event.surface = xsurface;
		resize_event.edges = net_wm_edges_to_wlr(detail);
		wlr_signal_emit_safe(&xsurface->events.request_resize, &resize_event);
		break;
	case _NET_WM_MOVERESIZE_CANCEL:
		// handled by the compositor
		break;
	}
}

#define _NET_WM_STATE_REMOVE	0
#define _NET_WM_STATE_ADD	1
#define _NET_WM_STATE_TOGGLE	2

static bool update_state(int action, bool *state) {
	int new_state, changed;

	switch (action) {
		case _NET_WM_STATE_REMOVE:
			new_state = false;
			break;
		case _NET_WM_STATE_ADD:
			new_state = true;
			break;
		case _NET_WM_STATE_TOGGLE:
			new_state = !*state;
			break;
		default:
			return false;
	}

	changed = (*state != new_state);
	*state = new_state;

	return changed;
}

static inline bool xsurface_is_maximized(
		struct wlr_xwayland_surface *xsurface) {
	return xsurface->maximized_horz && xsurface->maximized_vert;
}

static void xwm_handle_net_wm_state_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *client_message) {
	struct wlr_xwayland_surface *xsurface =
		lookup_surface(xwm, client_message->window);
	if (!xsurface) {
		return;
	}
	if (client_message->format != 32) {
		return;
	}

	bool fullscreen = xsurface->fullscreen;
	bool maximized = xsurface_is_maximized(xsurface);

	uint32_t action = client_message->data.data32[0];
	for (size_t i = 0; i < 2; ++i) {
		uint32_t property = client_message->data.data32[1 + i];

		if (property == xwm->atoms[_NET_WM_STATE_MODAL] &&
				update_state(action, &xsurface->modal)) {
			xsurface_set_net_wm_state(xsurface);
		} else if (property == xwm->atoms[_NET_WM_STATE_FULLSCREEN] &&
				update_state(action, &xsurface->fullscreen)) {
			xsurface_set_net_wm_state(xsurface);
		} else if (property == xwm->atoms[_NET_WM_STATE_MAXIMIZED_VERT] &&
				update_state(action, &xsurface->maximized_vert)) {
			xsurface_set_net_wm_state(xsurface);
		} else if (property == xwm->atoms[_NET_WM_STATE_MAXIMIZED_HORZ] &&
				update_state(action, &xsurface->maximized_horz)) {
			xsurface_set_net_wm_state(xsurface);
		}
	}
	// client_message->data.data32[3] is the source indication
	// all other values are set to 0

	if (fullscreen != xsurface->fullscreen) {
		if (xsurface->fullscreen) {
			xsurface->saved_width = xsurface->width;
			xsurface->saved_height = xsurface->height;
		}

		wlr_signal_emit_safe(&xsurface->events.request_fullscreen, xsurface);
	}

	if (maximized != xsurface_is_maximized(xsurface)) {
		if (xsurface_is_maximized(xsurface)) {
			xsurface->saved_width = xsurface->width;
			xsurface->saved_height = xsurface->height;
		}

		wlr_signal_emit_safe(&xsurface->events.request_maximize, xsurface);
	}
}

static void xwm_handle_wm_protocols_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	xcb_atom_t type = ev->data.data32[0];

	if (type == xwm->atoms[_NET_WM_PING]) {
		xcb_window_t window_id = ev->data.data32[2];

		struct wlr_xwayland_surface *surface = lookup_surface(xwm, window_id);
		if (surface == NULL) {
			return;
		}

		if (!surface->pinging) {
			return;
		}

		wl_event_source_timer_update(surface->ping_timer, 0);
		surface->pinging = false;
	} else {
		char *type_name = xwm_get_atom_name(xwm, type);
		wlr_log(WLR_DEBUG, "unhandled WM_PROTOCOLS client message %u (%s)",
			type, type_name);
		free(type_name);
	}
}

static void xwm_handle_net_active_window_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	struct wlr_xwayland_surface *surface = lookup_surface(xwm, ev->window);
	if (surface == NULL) {
		return;
	}
	wlr_signal_emit_safe(&surface->events.request_activate, surface);
}

static void xwm_handle_client_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	wlr_log(WLR_DEBUG, "XCB_CLIENT_MESSAGE (%u)", ev->window);

	if (ev->type == xwm->atoms[WL_SURFACE_ID]) {
		xwm_handle_surface_id_message(xwm, ev);
	} else if (ev->type == xwm->atoms[NET_WM_STATE]) {
		xwm_handle_net_wm_state_message(xwm, ev);
	} else if (ev->type == xwm->atoms[_NET_WM_MOVERESIZE]) {
		xwm_handle_net_wm_moveresize_message(xwm, ev);
	} else if (ev->type == xwm->atoms[WM_PROTOCOLS]) {
		xwm_handle_wm_protocols_message(xwm, ev);
	} else if (ev->type == xwm->atoms[_NET_ACTIVE_WINDOW]) {
		xwm_handle_net_active_window_message(xwm, ev);
	} else if (!xwm_handle_selection_client_message(xwm, ev)) {
		char *type_name = xwm_get_atom_name(xwm, ev->type);
		wlr_log(WLR_DEBUG, "unhandled x11 client message %u (%s)", ev->type,
			type_name);
		free(type_name);
	}
}

static void xwm_handle_focus_in(struct wlr_xwm *xwm,
		xcb_focus_in_event_t *ev) {
	// Do not interfere with grabs
	if (ev->mode == XCB_NOTIFY_MODE_GRAB ||
			ev->mode == XCB_NOTIFY_MODE_UNGRAB) {
		return;
	}

	// Do not let X clients change the focus behind the compositor's
	// back. Reset the focus to the old one if it changed.
	if (!xwm->focus_surface || ev->event != xwm->focus_surface->window_id) {
		xwm_send_focus_window(xwm, xwm->focus_surface);
	}
}

static void xwm_handle_xcb_error(struct wlr_xwm *xwm, xcb_value_error_t *ev) {
#ifdef WLR_HAS_XCB_ERRORS
	const char *major_name =
		xcb_errors_get_name_for_major_code(xwm->errors_context,
			ev->major_opcode);
	if (!major_name) {
		wlr_log(WLR_DEBUG, "xcb error happened, but could not get major name");
		goto log_raw;
	}

	const char *minor_name =
		xcb_errors_get_name_for_minor_code(xwm->errors_context,
			ev->major_opcode, ev->minor_opcode);

	const char *extension;
	const char *error_name =
		xcb_errors_get_name_for_error(xwm->errors_context,
			ev->error_code, &extension);
	if (!error_name) {
		wlr_log(WLR_DEBUG, "xcb error happened, but could not get error name");
		goto log_raw;
	}

	wlr_log(WLR_ERROR, "xcb error: op %s (%s), code %s (%s), sequence %"PRIu16", value %"PRIu32,
		major_name, minor_name ? minor_name : "no minor",
		error_name, extension ? extension : "no extension",
		ev->sequence, ev->bad_value);

	return;
log_raw:
#endif
	wlr_log(WLR_ERROR,
		"xcb error: op %"PRIu8":%"PRIu16", code %"PRIu8", sequence %"PRIu16", value %"PRIu32,
		ev->major_opcode, ev->minor_opcode, ev->error_code,
		ev->sequence, ev->bad_value);

}

static void xwm_handle_unhandled_event(struct wlr_xwm *xwm, xcb_generic_event_t *ev) {
#ifdef WLR_HAS_XCB_ERRORS
	const char *extension;
	const char *event_name =
		xcb_errors_get_name_for_xcb_event(xwm->errors_context,
			ev, &extension);
	if (!event_name) {
		wlr_log(WLR_DEBUG, "no name for unhandled event: %u",
			ev->response_type);
		return;
	}

	wlr_log(WLR_DEBUG, "unhandled X11 event: %s (%u)", event_name, ev->response_type);
#else
	wlr_log(WLR_DEBUG, "unhandled X11 event: %u", ev->response_type);
#endif
}

static int x11_event_handler(int fd, uint32_t mask, void *data) {
	int count = 0;
	xcb_generic_event_t *event;
	struct wlr_xwm *xwm = data;

	while ((event = xcb_poll_for_event(xwm->xcb_conn))) {
		count++;

		if (xwm->xwayland->user_event_handler &&
				xwm->xwayland->user_event_handler(xwm, event)) {
			break;
		}

		if (xwm_handle_selection_event(xwm, event)) {
			free(event);
			continue;
		}

		switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
		case XCB_CREATE_NOTIFY:
			xwm_handle_create_notify(xwm, (xcb_create_notify_event_t *)event);
			break;
		case XCB_DESTROY_NOTIFY:
			xwm_handle_destroy_notify(xwm, (xcb_destroy_notify_event_t *)event);
			break;
		case XCB_CONFIGURE_REQUEST:
			xwm_handle_configure_request(xwm,
				(xcb_configure_request_event_t *)event);
			break;
		case XCB_CONFIGURE_NOTIFY:
			xwm_handle_configure_notify(xwm,
				(xcb_configure_notify_event_t *)event);
			break;
		case XCB_MAP_REQUEST:
			xwm_handle_map_request(xwm, (xcb_map_request_event_t *)event);
			break;
		case XCB_MAP_NOTIFY:
			xwm_handle_map_notify(xwm, (xcb_map_notify_event_t *)event);
			break;
		case XCB_UNMAP_NOTIFY:
			xwm_handle_unmap_notify(xwm, (xcb_unmap_notify_event_t *)event);
			break;
		case XCB_PROPERTY_NOTIFY:
			xwm_handle_property_notify(xwm,
				(xcb_property_notify_event_t *)event);
			break;
		case XCB_CLIENT_MESSAGE:
			xwm_handle_client_message(xwm, (xcb_client_message_event_t *)event);
			break;
		case XCB_FOCUS_IN:
			xwm_handle_focus_in(xwm, (xcb_focus_in_event_t *)event);
			break;
		case 0:
			xwm_handle_xcb_error(xwm, (xcb_value_error_t *)event);
			break;
		default:
			xwm_handle_unhandled_event(xwm, event);
			break;
		}
		free(event);
	}

	if (count) {
		xcb_flush(xwm->xcb_conn);
	}

	return count;
}

static void handle_compositor_new_surface(struct wl_listener *listener,
		void *data) {
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, compositor_new_surface);
	struct wlr_surface *surface = data;
	if (wl_resource_get_client(surface->resource) != xwm->xwayland->client) {
		return;
	}

	wlr_log(WLR_DEBUG, "New xwayland surface: %p", surface);

	uint32_t surface_id = wl_resource_get_id(surface->resource);
	struct wlr_xwayland_surface *xsurface;
	wl_list_for_each(xsurface, &xwm->unpaired_surfaces, unpaired_link) {
		if (xsurface->surface_id == surface_id) {
			xwm_map_shell_surface(xwm, xsurface, surface);
			xsurface->surface_id = 0;
			wl_list_remove(&xsurface->unpaired_link);
			xcb_flush(xwm->xcb_conn);
			return;
		}
	}
}

static void handle_compositor_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, compositor_destroy);
	wl_list_remove(&xwm->compositor_new_surface.link);
	wl_list_remove(&xwm->compositor_destroy.link);
	wl_list_init(&xwm->compositor_new_surface.link);
	wl_list_init(&xwm->compositor_destroy.link);
}

void wlr_xwayland_surface_activate(struct wlr_xwayland_surface *xsurface,
		bool activated) {
	struct wlr_xwayland_surface *focused = xsurface->xwm->focus_surface;
	if (activated) {
		xwm_surface_activate(xsurface->xwm, xsurface);
	} else if (focused == xsurface) {
		xwm_surface_activate(xsurface->xwm, NULL);
	}
}

void wlr_xwayland_surface_configure(struct wlr_xwayland_surface *xsurface,
		int16_t x, int16_t y, uint16_t width, uint16_t height) {
	xsurface->x = x;
	xsurface->y = y;
	xsurface->width = width;
	xsurface->height = height;

	struct wlr_xwm *xwm = xsurface->xwm;
	uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
		XCB_CONFIG_WINDOW_BORDER_WIDTH;
	uint32_t values[] = {x, y, width, height, 0};
	xcb_configure_window(xwm->xcb_conn, xsurface->window_id, mask, values);
	xcb_flush(xwm->xcb_conn);
}

void wlr_xwayland_surface_close(struct wlr_xwayland_surface *xsurface) {
	struct wlr_xwm *xwm = xsurface->xwm;

	bool supports_delete = false;
	for (size_t i = 0; i < xsurface->protocols_len; i++) {
		if (xsurface->protocols[i] == xwm->atoms[WM_DELETE_WINDOW]) {
			supports_delete = true;
			break;
		}
	}

	if (supports_delete) {
		xcb_client_message_data_t message_data = {0};
		message_data.data32[0] = xwm->atoms[WM_DELETE_WINDOW];
		message_data.data32[1] = XCB_CURRENT_TIME;
		xwm_send_wm_message(xsurface, &message_data, XCB_EVENT_MASK_NO_EVENT);
	} else {
		xcb_kill_client(xwm->xcb_conn, xsurface->window_id);
		xcb_flush(xwm->xcb_conn);
	}
}

void xwm_destroy(struct wlr_xwm *xwm) {
	if (!xwm) {
		return;
	}
	xwm_selection_finish(xwm);
	if (xwm->cursor) {
		xcb_free_cursor(xwm->xcb_conn, xwm->cursor);
	}
	if (xwm->colormap) {
		xcb_free_colormap(xwm->xcb_conn, xwm->colormap);
	}
	if (xwm->window) {
		xcb_destroy_window(xwm->xcb_conn, xwm->window);
	}
	if (xwm->event_source) {
		wl_event_source_remove(xwm->event_source);
	}
#ifdef WLR_HAS_XCB_ERRORS
	if (xwm->errors_context) {
		xcb_errors_context_free(xwm->errors_context);
	}
#endif
	struct wlr_xwayland_surface *xsurface, *tmp;
	wl_list_for_each_safe(xsurface, tmp, &xwm->surfaces, link) {
		xwayland_surface_destroy(xsurface);
	}
	wl_list_for_each_safe(xsurface, tmp, &xwm->unpaired_surfaces, link) {
		xwayland_surface_destroy(xsurface);
	}
	wl_list_remove(&xwm->compositor_new_surface.link);
	wl_list_remove(&xwm->compositor_destroy.link);
	xcb_disconnect(xwm->xcb_conn);

	free(xwm);
}

static void xwm_get_resources(struct wlr_xwm *xwm) {
	xcb_prefetch_extension_data(xwm->xcb_conn, &xcb_xfixes_id);
	xcb_prefetch_extension_data(xwm->xcb_conn, &xcb_composite_id);

	size_t i;
	xcb_intern_atom_cookie_t cookies[ATOM_LAST];

	for (i = 0; i < ATOM_LAST; i++) {
		cookies[i] =
			xcb_intern_atom(xwm->xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
	}
	for (i = 0; i < ATOM_LAST; i++) {
		xcb_generic_error_t *error;
		xcb_intern_atom_reply_t *reply =
			xcb_intern_atom_reply(xwm->xcb_conn, cookies[i], &error);
		if (reply && !error) {
			xwm->atoms[i] = reply->atom;
		}
		free(reply);

		if (error) {
			wlr_log(WLR_ERROR, "could not resolve atom %s, x11 error code %d",
				atom_map[i], error->error_code);
			free(error);
			return;
		}
	}

	xwm->xfixes = xcb_get_extension_data(xwm->xcb_conn, &xcb_xfixes_id);

	if (!xwm->xfixes || !xwm->xfixes->present) {
		wlr_log(WLR_DEBUG, "xfixes not available");
	}

	xcb_xfixes_query_version_cookie_t xfixes_cookie;
	xcb_xfixes_query_version_reply_t *xfixes_reply;
	xfixes_cookie =
		xcb_xfixes_query_version(xwm->xcb_conn, XCB_XFIXES_MAJOR_VERSION,
			XCB_XFIXES_MINOR_VERSION);
	xfixes_reply =
		xcb_xfixes_query_version_reply(xwm->xcb_conn, xfixes_cookie, NULL);

	wlr_log(WLR_DEBUG, "xfixes version: %d.%d",
		xfixes_reply->major_version, xfixes_reply->minor_version);

	free(xfixes_reply);
}

static void xwm_create_wm_window(struct wlr_xwm *xwm) {
	static const char name[] = "wlroots wm";

	xwm->window = xcb_generate_id(xwm->xcb_conn);

	xcb_create_window(xwm->xcb_conn,
		XCB_COPY_FROM_PARENT,
		xwm->window,
		xwm->screen->root,
		0, 0,
		10, 10,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		xwm->screen->root_visual,
		0, NULL);

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->window,
		xwm->atoms[_NET_WM_NAME],
		xwm->atoms[UTF8_STRING],
		8, // format
		strlen(name), name);

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->screen->root,
		xwm->atoms[_NET_SUPPORTING_WM_CHECK],
		XCB_ATOM_WINDOW,
		32, // format
		1, &xwm->window);

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->window,
		xwm->atoms[_NET_SUPPORTING_WM_CHECK],
		XCB_ATOM_WINDOW,
		32, // format
		1, &xwm->window);

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->window,
		xwm->atoms[WM_S0],
		XCB_CURRENT_TIME);

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->window,
		xwm->atoms[NET_WM_S0],
		XCB_CURRENT_TIME);
}

// TODO use me to support 32 bit color somehow
static void xwm_get_visual_and_colormap(struct wlr_xwm *xwm) {
	xcb_depth_iterator_t d_iter;
	xcb_visualtype_iterator_t vt_iter;
	xcb_visualtype_t *visualtype;

	d_iter = xcb_screen_allowed_depths_iterator(xwm->screen);
	visualtype = NULL;
	while (d_iter.rem > 0) {
		if (d_iter.data->depth == 32) {
			vt_iter = xcb_depth_visuals_iterator(d_iter.data);
			visualtype = vt_iter.data;
			break;
		}

		xcb_depth_next(&d_iter);
	}

	if (visualtype == NULL) {
		wlr_log(WLR_DEBUG, "No 32 bit visualtype\n");
		return;
	}

	xwm->visual_id = visualtype->visual_id;
	xwm->colormap = xcb_generate_id(xwm->xcb_conn);
	xcb_create_colormap(xwm->xcb_conn,
		XCB_COLORMAP_ALLOC_NONE,
		xwm->colormap,
		xwm->screen->root,
		xwm->visual_id);
}

static void xwm_get_render_format(struct wlr_xwm *xwm) {
	xcb_render_query_pict_formats_cookie_t cookie =
		xcb_render_query_pict_formats(xwm->xcb_conn);
	xcb_render_query_pict_formats_reply_t *reply =
		xcb_render_query_pict_formats_reply(xwm->xcb_conn, cookie, NULL);
	if (!reply) {
		wlr_log(WLR_ERROR, "Did not get any reply from xcb_render_query_pict_formats");
		return;
	}
	xcb_render_pictforminfo_iterator_t iter =
		xcb_render_query_pict_formats_formats_iterator(reply);
	xcb_render_pictforminfo_t *format = NULL;
	while (iter.rem > 0) {
		if (iter.data->depth == 32) {
			format = iter.data;
			break;
		}

		xcb_render_pictforminfo_next(&iter);
	}

	if (format == NULL) {
		wlr_log(WLR_DEBUG, "No 32 bit render format");
		free(reply);
		return;
	}

	xwm->render_format_id = format->id;
	free(reply);
}

void xwm_set_cursor(struct wlr_xwm *xwm, const uint8_t *pixels, uint32_t stride,
		uint32_t width, uint32_t height, int32_t hotspot_x, int32_t hotspot_y) {
	if (!xwm->render_format_id) {
		wlr_log(WLR_ERROR, "Cannot set xwm cursor: no render format available");
		return;
	}
	if (xwm->cursor) {
		xcb_free_cursor(xwm->xcb_conn, xwm->cursor);
	}

	int depth = 32;

	xcb_pixmap_t pix = xcb_generate_id(xwm->xcb_conn);
	xcb_create_pixmap(xwm->xcb_conn, depth, pix, xwm->screen->root, width,
		height);

	xcb_render_picture_t pic = xcb_generate_id(xwm->xcb_conn);
	xcb_render_create_picture(xwm->xcb_conn, pic, pix, xwm->render_format_id,
		0, 0);

	xcb_gcontext_t gc = xcb_generate_id(xwm->xcb_conn);
	xcb_create_gc(xwm->xcb_conn, gc, pix, 0, NULL);

	xcb_put_image(xwm->xcb_conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pix, gc,
		width, height, 0, 0, 0, depth, stride * height * sizeof(uint8_t),
		pixels);
	xcb_free_gc(xwm->xcb_conn, gc);

	xwm->cursor = xcb_generate_id(xwm->xcb_conn);
	xcb_render_create_cursor(xwm->xcb_conn, xwm->cursor, pic, hotspot_x,
		hotspot_y);
	xcb_free_pixmap(xwm->xcb_conn, pix);

	uint32_t values[] = {xwm->cursor};
	xcb_change_window_attributes(xwm->xcb_conn, xwm->screen->root,
		XCB_CW_CURSOR, values);
	xcb_flush(xwm->xcb_conn);
}

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland) {
	struct wlr_xwm *xwm = calloc(1, sizeof(struct wlr_xwm));
	if (xwm == NULL) {
		return NULL;
	}

	xwm->xwayland = wlr_xwayland;
	wl_list_init(&xwm->surfaces);
	wl_list_init(&xwm->unpaired_surfaces);
	xwm->ping_timeout = 10000;

	xwm->xcb_conn = xcb_connect_to_fd(wlr_xwayland->wm_fd[0], NULL);

	int rc = xcb_connection_has_error(xwm->xcb_conn);
	if (rc) {
		wlr_log(WLR_ERROR, "xcb connect failed: %d", rc);
		close(wlr_xwayland->wm_fd[0]);
		free(xwm);
		return NULL;
	}

#ifdef WLR_HAS_XCB_ERRORS
	if (xcb_errors_context_new(xwm->xcb_conn, &xwm->errors_context)) {
		wlr_log(WLR_ERROR, "Could not allocate error context");
		xwm_destroy(xwm);
		return NULL;
	}
#endif
	xcb_screen_iterator_t screen_iterator =
		xcb_setup_roots_iterator(xcb_get_setup(xwm->xcb_conn));
	xwm->screen = screen_iterator.data;

	struct wl_event_loop *event_loop = wl_display_get_event_loop(
		wlr_xwayland->wl_display);
	xwm->event_source =
		wl_event_loop_add_fd(event_loop,
			wlr_xwayland->wm_fd[0],
			WL_EVENT_READABLE,
			x11_event_handler,
			xwm);
	wl_event_source_check(xwm->event_source);

	xwm_get_resources(xwm);
	xwm_get_visual_and_colormap(xwm);
	xwm_get_render_format(xwm);

	uint32_t values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
			XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
			XCB_EVENT_MASK_PROPERTY_CHANGE,
	};
	xcb_change_window_attributes(xwm->xcb_conn,
		xwm->screen->root,
		XCB_CW_EVENT_MASK,
		values);

	xcb_composite_redirect_subwindows(xwm->xcb_conn,
		xwm->screen->root,
		XCB_COMPOSITE_REDIRECT_MANUAL);

	xcb_atom_t supported[] = {
		xwm->atoms[NET_WM_STATE],
		xwm->atoms[_NET_ACTIVE_WINDOW],
		xwm->atoms[_NET_WM_MOVERESIZE],
		xwm->atoms[_NET_WM_STATE_MODAL],
		xwm->atoms[_NET_WM_STATE_FULLSCREEN],
		xwm->atoms[_NET_WM_STATE_MAXIMIZED_VERT],
		xwm->atoms[_NET_WM_STATE_MAXIMIZED_HORZ],
	};
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->screen->root,
		xwm->atoms[NET_SUPPORTED],
		XCB_ATOM_ATOM,
		32,
		sizeof(supported)/sizeof(*supported),
		supported);

	xcb_flush(xwm->xcb_conn);

	xwm_set_net_active_window(xwm, XCB_WINDOW_NONE);

	xwm_selection_init(xwm);

	xwm->compositor_new_surface.notify = handle_compositor_new_surface;
	wl_signal_add(&wlr_xwayland->compositor->events.new_surface,
		&xwm->compositor_new_surface);
	xwm->compositor_destroy.notify = handle_compositor_destroy;
	wl_signal_add(&wlr_xwayland->compositor->events.destroy,
		&xwm->compositor_destroy);

	xwm_create_wm_window(xwm);

	xcb_flush(xwm->xcb_conn);

	return xwm;
}

void wlr_xwayland_surface_set_maximized(struct wlr_xwayland_surface *surface,
		bool maximized) {
	surface->maximized_horz = maximized;
	surface->maximized_vert = maximized;
	xsurface_set_net_wm_state(surface);
	xcb_flush(surface->xwm->xcb_conn);
}

void wlr_xwayland_surface_set_fullscreen(struct wlr_xwayland_surface *surface,
		bool fullscreen) {
	surface->fullscreen = fullscreen;
	xsurface_set_net_wm_state(surface);
	xcb_flush(surface->xwm->xcb_conn);
}

bool xwm_atoms_contains(struct wlr_xwm *xwm, xcb_atom_t *atoms,
		size_t num_atoms, enum atom_name needle) {
	xcb_atom_t atom = xwm->atoms[needle];

	for (size_t i = 0; i < num_atoms; ++i) {
		if (atom == atoms[i]) {
			return true;
		}
	}

	return false;
}

void wlr_xwayland_surface_ping(struct wlr_xwayland_surface *surface) {
	xcb_client_message_data_t data = { 0 };
	data.data32[0] = surface->xwm->atoms[_NET_WM_PING];
	data.data32[1] = XCB_CURRENT_TIME;
	data.data32[2] = surface->window_id;

	xwm_send_wm_message(surface, &data, XCB_EVENT_MASK_NO_EVENT);

	wl_event_source_timer_update(surface->ping_timer,
		surface->xwm->ping_timeout);
	surface->pinging = true;
}

bool wlr_xwayland_or_surface_wants_focus(
		const struct wlr_xwayland_surface *surface) {
	bool ret = true;
	static enum atom_name needles[] = {
		NET_WM_WINDOW_TYPE_COMBO,
		NET_WM_WINDOW_TYPE_DND,
		NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
		NET_WM_WINDOW_TYPE_MENU,
		NET_WM_WINDOW_TYPE_NOTIFICATION,
		NET_WM_WINDOW_TYPE_POPUP_MENU,
		NET_WM_WINDOW_TYPE_SPLASH,
		NET_WM_WINDOW_TYPE_TOOLTIP,
		NET_WM_WINDOW_TYPE_UTILITY,
	};
	for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i) {
		if (xwm_atoms_contains(surface->xwm, surface->window_type,
				surface->window_type_len, needles[i])) {
			ret = false;
		}
	}

	return ret;
}
