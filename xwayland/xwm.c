#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <unistd.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/xcb_image.h>
#include <xcb/render.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_surface.h"
#include "wlr/xwayland.h"
#include "wlr/xcursor.h"
#include "xwm.h"

#ifdef HAS_XCB_ICCCM
	#include <xcb/xcb_icccm.h>
#endif

const char *atom_map[ATOM_LAST] = {
	"WL_SURFACE_ID",
	"WM_DELETE_WINDOW",
	"WM_PROTOCOLS",
	"WM_HINTS",
	"WM_NORMAL_HINTS",
	"WM_SIZE_HINTS",
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
	"_NET_WM_STATE_FULLSCREEN",
	"_NET_WM_STATE_MAXIMIZED_VERT",
	"_NET_WM_STATE_MAXIMIZED_HORZ",
	"WM_STATE",
	"CLIPBOARD",
	"_WL_SELECTION",
	"TARGETS",
	"CLIPBOARD_MANAGER",
	"INCR",
	"TEXT",
	"TIMESTAMP",
};

/* General helpers */
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

static struct wlr_xwayland_surface *wlr_xwayland_surface_create(
		struct wlr_xwm *xwm, xcb_window_t window_id, int16_t x, int16_t y,
		uint16_t width, uint16_t height, bool override_redirect) {
	struct wlr_xwayland_surface *surface =
		calloc(1, sizeof(struct wlr_xwayland_surface));
	if (!surface) {
		wlr_log(L_ERROR, "Could not allocate wlr xwayland surface");
		return NULL;
	}

	xcb_get_geometry_cookie_t geometry_cookie =
		xcb_get_geometry(xwm->xcb_conn, window_id);

	uint32_t values[1];
	values[0] =
		XCB_EVENT_MASK_FOCUS_CHANGE |
		XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes(xwm->xcb_conn, window_id,
		XCB_CW_EVENT_MASK, &values);

	surface->xwm = xwm;
	surface->window_id = window_id;
	surface->x = x;
	surface->y = y;
	surface->width = width;
	surface->height = height;
	surface->override_redirect = override_redirect;
	wl_list_insert(&xwm->surfaces, &surface->link);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.request_configure);
	wl_signal_init(&surface->events.request_move);
	wl_signal_init(&surface->events.request_resize);
	wl_signal_init(&surface->events.request_maximize);
	wl_signal_init(&surface->events.request_fullscreen);
	wl_signal_init(&surface->events.map_notify);
	wl_signal_init(&surface->events.unmap_notify);
	wl_signal_init(&surface->events.set_class);
	wl_signal_init(&surface->events.set_title);
	wl_signal_init(&surface->events.set_parent);
	wl_signal_init(&surface->events.set_pid);
	wl_signal_init(&surface->events.set_window_type);

	xcb_get_geometry_reply_t *geometry_reply =
		xcb_get_geometry_reply(xwm->xcb_conn, geometry_cookie, NULL);

	if (geometry_reply != NULL) {
		surface->has_alpha = geometry_reply->depth == 32;
	}

	free(geometry_reply);

	return surface;
}

static void xwm_set_net_active_window(struct wlr_xwm *xwm,
		xcb_window_t window) {
	xcb_change_property(xwm->xcb_conn, XCB_PROP_MODE_REPLACE,
			xwm->screen->root, xwm->atoms[_NET_ACTIVE_WINDOW],
			xwm->atoms[WINDOW], 32, 1, &window);
}

static void xwm_send_focus_window(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface) {
	if (!xsurface) {
		xcb_set_input_focus_checked(xwm->xcb_conn,
			XCB_INPUT_FOCUS_POINTER_ROOT,
			XCB_NONE, XCB_CURRENT_TIME);
		return;
	} else if (xsurface->override_redirect) {
		return;
	}

	xcb_client_message_event_t client_message;
	client_message.response_type = XCB_CLIENT_MESSAGE;
	client_message.format = 32;
	client_message.window = xsurface->window_id;
	client_message.type = xwm->atoms[WM_PROTOCOLS];
	client_message.data.data32[0] = xwm->atoms[WM_TAKE_FOCUS];
	client_message.data.data32[1] = XCB_TIME_CURRENT_TIME;

	xcb_send_event(xwm->xcb_conn, 0, xsurface->window_id,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*)&client_message);

	xcb_set_input_focus(xwm->xcb_conn, XCB_INPUT_FOCUS_POINTER_ROOT,
		xsurface->window_id, XCB_CURRENT_TIME);

	uint32_t values[1];
	values[0] = XCB_STACK_MODE_ABOVE;
	xcb_configure_window(xwm->xcb_conn, xsurface->window_id,
		XCB_CONFIG_WINDOW_STACK_MODE, values);
}


void xwm_surface_activate(struct wlr_xwm *xwm,
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
	xwm_set_selection_owner(xwm);

	xcb_flush(xwm->xcb_conn);
}

static void xsurface_set_net_wm_state(struct wlr_xwayland_surface *xsurface) {
	struct wlr_xwm *xwm = xsurface->xwm;
	uint32_t property[3];
	int i;

	i = 0;
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

static void wlr_xwayland_surface_destroy(
		struct wlr_xwayland_surface *xsurface) {
	wl_signal_emit(&xsurface->events.destroy, xsurface);

	if (xsurface == xsurface->xwm->focus_surface) {
		xwm_surface_activate(xsurface->xwm, NULL);
	}

	wl_list_remove(&xsurface->link);

	if (xsurface->surface_id) {
		wl_list_remove(&xsurface->unpaired_link);
	}

	if (xsurface->surface) {
		wl_list_remove(&xsurface->surface_destroy.link);
		wl_list_remove(&xsurface->surface_commit.link);
	}

	free(xsurface->title);
	free(xsurface->class);
	free(xsurface->instance);
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

	wlr_log(L_DEBUG, "XCB_ATOM_WM_CLASS: %s %s", surface->instance,
		surface->class);
	wl_signal_emit(&surface->events.set_class, surface);
}

static void read_surface_title(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_STRING &&
			reply->type != xwm->atoms[UTF8_STRING]) {
		return;
	}

	// TODO: if reply->type == XCB_ATOM_STRING, uses latin1 encoding
	// if reply->type == xwm->atoms[UTF8_STRING], uses utf8 encoding

	size_t len = xcb_get_property_value_length(reply);
	char *title = xcb_get_property_value(reply);

	free(xsurface->title);
	if (len > 0) {
		xsurface->title = strndup(title, len);
	} else {
		xsurface->title = NULL;
	}

	wlr_log(L_DEBUG, "XCB_ATOM_WM_NAME: %s", xsurface->title);
	wl_signal_emit(&xsurface->events.set_title, xsurface);
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

	wlr_log(L_DEBUG, "XCB_ATOM_WM_TRANSIENT_FOR: %p", xid);
	wl_signal_emit(&xsurface->events.set_parent, xsurface);
}

static void read_surface_pid(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_CARDINAL) {
		return;
	}

	pid_t *pid = xcb_get_property_value(reply);
	xsurface->pid = *pid;
	wlr_log(L_DEBUG, "NET_WM_PID %d", xsurface->pid);
	wl_signal_emit(&xsurface->events.set_pid, xsurface);
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

	wlr_log(L_DEBUG, "NET_WM_WINDOW_TYPE (%zu)", atoms_len);
	wl_signal_emit(&xsurface->events.set_window_type, xsurface);
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

	wlr_log(L_DEBUG, "WM_PROTOCOLS (%zu)", atoms_len);
}

#ifdef HAS_XCB_ICCCM
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

	wlr_log(L_DEBUG, "WM_HINTS (%d)", reply->value_len);
}
#else
static void read_surface_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	// Do nothing
}
#endif

#ifdef HAS_XCB_ICCCM
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

	wlr_log(L_DEBUG, "WM_NORMAL_HINTS (%d)", reply->value_len);
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
	}

	wlr_log(L_DEBUG, "MOTIF_WM_HINTS (%d)", reply->value_len);
}

static void read_surface_net_wm_state(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		xcb_get_property_reply_t *reply) {
	xsurface->fullscreen = 0;
	xcb_atom_t *atom = xcb_get_property_value(reply);
	for (uint32_t i = 0; i < reply->value_len; i++) {
		if (atom[i] == xwm->atoms[_NET_WM_STATE_FULLSCREEN])
			xsurface->fullscreen = true;
		if (atom[i] == xwm->atoms[_NET_WM_STATE_MAXIMIZED_VERT])
			xsurface->maximized_vert = true;
		if (atom[i] == xwm->atoms[_NET_WM_STATE_MAXIMIZED_HORZ])
			xsurface->maximized_horz = true;
	}
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
	} else {
		wlr_log(L_DEBUG, "unhandled x11 property %u", property);
	}

	free(reply);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_surface *xsurface =
		wl_container_of(listener, xsurface, surface_commit);

	if (!xsurface->added &&
			wlr_surface_has_buffer(xsurface->surface) &&
			xsurface->mapped) {
		wl_signal_emit(&xsurface->xwm->xwayland->events.new_surface, xsurface);
		xsurface->added = true;
	}
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_surface *xsurface =
		wl_container_of(listener, xsurface, surface_destroy);

	xsurface->surface = NULL;
	// TODO destroy xwayland surface?
}

static void xwm_map_shell_surface(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xsurface,
		struct wlr_surface *surface) {
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

	xsurface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surface->events.commit, &xsurface->surface_commit);

	xsurface->surface_destroy.notify = handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &xsurface->surface_destroy);

	xsurface->mapped = true;
	wl_signal_emit(&xsurface->events.map_notify, xsurface);
}

static void xwm_handle_create_notify(struct wlr_xwm *xwm,
		xcb_create_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CREATE_NOTIFY (%u)", ev->window);
	wlr_xwayland_surface_create(xwm, ev->window, ev->x, ev->y,
		ev->width, ev->height, ev->override_redirect);
}

static void xwm_handle_destroy_notify(struct wlr_xwm *xwm,
		xcb_destroy_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_DESTROY_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		return;
	}
	wlr_xwayland_surface_destroy(xsurface);
}

static void xwm_handle_configure_request(struct wlr_xwm *xwm,
		xcb_configure_request_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CONFIGURE_REQUEST (%u) [%ux%u+%d,%d]", ev->window,
		ev->width, ev->height, ev->x, ev->y);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		return;
	}

	// TODO: handle ev->{parent,sibling}?

	if (xsurface->surface == NULL) {
		// Surface has not been mapped yet
		wlr_xwayland_surface_configure(xwm->xwayland, xsurface, ev->x, ev->y,
			ev->width, ev->height);
	} else {
		struct wlr_xwayland_surface_configure_event *wlr_event =
			calloc(1, sizeof(struct wlr_xwayland_surface_configure_event));
		if (wlr_event == NULL) {
			return;
		}

		wlr_event->surface = xsurface;
		wlr_event->x = ev->x;
		wlr_event->y = ev->y;
		wlr_event->width = ev->width;
		wlr_event->height = ev->height;

		wl_signal_emit(&xsurface->events.request_configure, wlr_event);

		free(wlr_event);
	}
}

static void xwm_handle_configure_notify(struct wlr_xwm *xwm,
		xcb_configure_notify_event_t *ev) {
	struct wlr_xwayland_surface *xsurface =
		lookup_surface(xwm, ev->window);

	if (!xsurface) {
		return;
	}

	xsurface->x = ev->x;
	xsurface->y = ev->y;
	xsurface->width = ev->width;
	xsurface->height = ev->height;
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
	wlr_log(L_DEBUG, "XCB_MAP_REQUEST (%u)", ev->window);
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
	wlr_log(L_DEBUG, "XCB_MAP_NOTIFY (%u)", ev->window);
}

static void xwm_handle_unmap_notify(struct wlr_xwm *xwm,
		xcb_unmap_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_UNMAP_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *xsurface = lookup_surface(xwm, ev->window);
	if (xsurface == NULL) {
		return;
	}

	if (xsurface->surface_id) {
		// Make sure we're not on the unpaired surface list or we
		// could be assigned a surface during surface creation that
		// was mapped before this unmap request.
		wl_list_remove(&xsurface->unpaired_link);
		xsurface->surface_id = 0;
	}

	if (xsurface->surface) {
		wl_list_remove(&xsurface->surface_commit.link);
		wl_list_remove(&xsurface->surface_destroy.link);
	}
	xsurface->surface = NULL;

	if (xsurface->mapped) {
		xsurface->mapped = false;
		wl_signal_emit(&xsurface->events.unmap_notify, xsurface);
	}

	xsurface_set_wm_state(xsurface, ICCCM_WITHDRAWN_STATE);
}

static void xwm_handle_property_notify(struct wlr_xwm *xwm,
		xcb_property_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_PROPERTY_NOTIFY (%u)", ev->window);
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
		wlr_log(L_DEBUG,
			"client message WL_SURFACE_ID but no new window %u ?",
			ev->window);
		return;
	}
	/* Check if we got notified after wayland surface create event */
	uint32_t id = ev->data.data32[0];
	struct wl_resource *resource =
		wl_client_get_object(xwm->xwayland->client, id);
	if (resource) {
		struct wlr_surface *surface = wl_resource_get_user_data(resource);
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

static void xwm_handle_net_wm_moveresize_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	// same as xdg-toplevel-v6
	// TODO need a common enum for this
	static const int map[] = {
		5, 1, 9, 8, 10, 2, 6, 4
	};

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
		wl_signal_emit(&xsurface->events.request_move, &move_event);
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
		resize_event.edges = map[detail];
		wl_signal_emit(&xsurface->events.request_resize, &resize_event);
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

		if (property == xwm->atoms[_NET_WM_STATE_FULLSCREEN] &&
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

		wl_signal_emit(&xsurface->events.request_fullscreen, xsurface);
	}

	if (maximized != xsurface_is_maximized(xsurface)) {
		if (xsurface_is_maximized(xsurface)) {
			xsurface->saved_width = xsurface->width;
			xsurface->saved_height = xsurface->height;
		}

		wl_signal_emit(&xsurface->events.request_maximize, xsurface);
	}
}

static void xwm_handle_client_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CLIENT_MESSAGE (%u)", ev->window);

	if (ev->type == xwm->atoms[WL_SURFACE_ID]) {
		xwm_handle_surface_id_message(xwm, ev);
	} else if (ev->type == xwm->atoms[NET_WM_STATE]) {
		xwm_handle_net_wm_state_message(xwm, ev);
	} else if (ev->type == xwm->atoms[_NET_WM_MOVERESIZE]) {
		xwm_handle_net_wm_moveresize_message(xwm, ev);
	} else {
		wlr_log(L_DEBUG, "unhandled x11 client message %u", ev->type);
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

/* This is in xcb/xcb_event.h, but pulling xcb-util just for a constant
 * others redefine anyway is meh
 */
#define XCB_EVENT_RESPONSE_TYPE_MASK (0x7f)
static int x11_event_handler(int fd, uint32_t mask, void *data) {
	int count = 0;
	xcb_generic_event_t *event;
	struct wlr_xwm *xwm = data;

	while ((event = xcb_poll_for_event(xwm->xcb_conn))) {
		count++;

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
		default:
			wlr_log(L_DEBUG, "X11 event: %d",
				event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK);
			break;
		}
		free(event);
	}

	if (count) {
		xcb_flush(xwm->xcb_conn);
	}

	return count;
}

static void handle_compositor_surface_create(struct wl_listener *listener,
		void *data) {
	struct wlr_surface *surface = data;
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, compositor_surface_create);
	if (wl_resource_get_client(surface->resource) != xwm->xwayland->client) {
		return;
	}

	wlr_log(L_DEBUG, "New xwayland surface: %p", surface);

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

void wlr_xwayland_surface_activate(struct wlr_xwayland *wlr_xwayland,
		struct wlr_xwayland_surface *xsurface, bool activated) {
	struct wlr_xwayland_surface *focused = wlr_xwayland->xwm->focus_surface;
	if (activated) {
		xwm_surface_activate(wlr_xwayland->xwm, xsurface);
	} else if (focused == xsurface) {
		xwm_surface_activate(wlr_xwayland->xwm, NULL);
	}
}

void wlr_xwayland_surface_configure(struct wlr_xwayland *wlr_xwayland,
		struct wlr_xwayland_surface *xsurface, int16_t x, int16_t y,
		uint16_t width, uint16_t height) {
	xsurface->x = x;
	xsurface->y = y;
	xsurface->width = width;
	xsurface->height = height;

	struct wlr_xwm *xwm = wlr_xwayland->xwm;
	uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
		XCB_CONFIG_WINDOW_BORDER_WIDTH;
	uint32_t values[] = {x, y, width, height, 0};
	xcb_configure_window(xwm->xcb_conn, xsurface->window_id, mask, values);
	xcb_flush(xwm->xcb_conn);
}

void wlr_xwayland_surface_close(struct wlr_xwayland *wlr_xwayland,
		struct wlr_xwayland_surface *xsurface) {
	struct wlr_xwm *xwm = wlr_xwayland->xwm;

	bool supports_delete = false;
	for (size_t i = 0; i < xsurface->protocols_len; i++) {
		if (xsurface->protocols[i] == xwm->atoms[WM_DELETE_WINDOW]) {
			supports_delete = true;
			break;
		}
	}

	if (supports_delete) {
		xcb_client_message_event_t ev = {0};
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = xsurface->window_id;
		ev.format = 32;
		ev.sequence = 0;
		ev.type = xwm->atoms[WM_PROTOCOLS];
		ev.data.data32[0] = xwm->atoms[WM_DELETE_WINDOW];
		ev.data.data32[1] = XCB_CURRENT_TIME;
		xcb_send_event(xwm->xcb_conn, 0,
			xsurface->window_id,
			XCB_EVENT_MASK_NO_EVENT,
			(char *)&ev);
	} else {
		xcb_kill_client(xwm->xcb_conn, xsurface->window_id);
	}

	xcb_flush(xwm->xcb_conn);
}

void xwm_destroy(struct wlr_xwm *xwm) {
	if (!xwm) {
		return;
	}
	if (xwm->cursor) {
		xcb_free_cursor(xwm->xcb_conn, xwm->cursor);
	}
	if (xwm->event_source) {
		wl_event_source_remove(xwm->event_source);
	}
	struct wlr_xwayland_surface *xsurface, *tmp;
	wl_list_for_each_safe(xsurface, tmp, &xwm->surfaces, link) {
		wlr_xwayland_surface_destroy(xsurface);
	}
	wl_list_remove(&xwm->compositor_surface_create.link);
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
		xcb_intern_atom_reply_t *reply;
		xcb_generic_error_t *error;

		reply = xcb_intern_atom_reply(xwm->xcb_conn, cookies[i], &error);

		if (reply && !error) {
			xwm->atoms[i] = reply->atom;
		}

		free(reply);

		if (error) {
			wlr_log(L_ERROR, "could not resolve atom %s, x11 error code %d",
				atom_map[i], error->error_code);
			free(error);
			return;
		}
	}

	xwm->xfixes = xcb_get_extension_data(xwm->xcb_conn, &xcb_xfixes_id);

	if (!xwm->xfixes || !xwm->xfixes->present) {
		wlr_log(L_DEBUG, "xfixes not available");
	}

	xcb_xfixes_query_version_cookie_t xfixes_cookie;
	xcb_xfixes_query_version_reply_t *xfixes_reply;
	xfixes_cookie =
		xcb_xfixes_query_version(xwm->xcb_conn, XCB_XFIXES_MAJOR_VERSION,
			XCB_XFIXES_MINOR_VERSION);
	xfixes_reply =
		xcb_xfixes_query_version_reply(xwm->xcb_conn, xfixes_cookie, NULL);

	wlr_log(L_DEBUG, "xfixes version: %d.%d",
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
		wlr_log(L_DEBUG, "No 32 bit visualtype\n");
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
		wlr_log(L_DEBUG, "No 32 bit render format");
		return;
	}

	xwm->render_format_id = format->id;
}

void xwm_set_cursor(struct wlr_xwm *xwm, const uint8_t *pixels, uint32_t stride,
		uint32_t width, uint32_t height, int32_t hotspot_x, int32_t hotspot_y) {
	if (!xwm->render_format_id) {
		wlr_log(L_ERROR, "Cannot set xwm cursor: no render format available");
		return;
	}
	if (xwm->cursor) {
		xcb_free_cursor(xwm->xcb_conn, xwm->cursor);
	}

	stride *= 4;
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

	xwm->xcb_conn = xcb_connect_to_fd(wlr_xwayland->wm_fd[0], NULL);

	int rc = xcb_connection_has_error(xwm->xcb_conn);
	if (rc) {
		wlr_log(L_ERROR, "xcb connect failed: %d", rc);
		close(wlr_xwayland->wm_fd[0]);
		free(xwm);
		return NULL;
	}

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

	xwm->compositor_surface_create.notify = handle_compositor_surface_create;
	wl_signal_add(&wlr_xwayland->compositor->events.create_surface,
		&xwm->compositor_surface_create);

	xwm_create_wm_window(xwm);

	xcb_flush(xwm->xcb_conn);

	return xwm;
}

void wlr_xwayland_surface_set_maximized(struct wlr_xwayland *wlr_xwayland,
	struct wlr_xwayland_surface *surface, bool maximized) {
	if (xsurface_is_maximized(surface) != maximized) {
		surface->maximized_horz = maximized;
		surface->maximized_vert = maximized;
		xsurface_set_net_wm_state(surface);
	}
}

void wlr_xwayland_surface_set_fullscreen(struct wlr_xwayland *wlr_xwayland,
	struct wlr_xwayland_surface *surface, bool fullscreen) {
	if (surface->fullscreen != fullscreen) {
		surface->fullscreen = fullscreen;
		xsurface_set_net_wm_state(surface);
	}
}
