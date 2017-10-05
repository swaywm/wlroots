#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <xcb/composite.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_surface.h"
#include "wlr/xwayland.h"
#include "xwm.h"

#ifdef HAS_XCB_ICCCM
	#include <xcb/xcb_icccm.h>
#endif

const char *atom_map[ATOM_LAST] = {
	"WL_SURFACE_ID",
	"WM_DELETE_WINDOW",
	"WM_HINTS",
	"WM_PROTOCOLS",
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
};

/* General helpers */
// TODO: replace this with hash table?
static struct wlr_xwayland_surface *lookup_surface(struct wl_list *list,
		xcb_window_t window_id) {
	struct wlr_xwayland_surface *surface;
	wl_list_for_each(surface, list, link) {
		if (surface->window_id == window_id) {
			return surface;
		}
	}
	return NULL;
}

static struct wlr_xwayland_surface *lookup_surface_any(struct wlr_xwm *xwm,
		xcb_window_t window_id) {
	struct wlr_xwayland_surface *surface;
	if ((surface = lookup_surface(&xwm->xwayland->displayable_surfaces, window_id)) ||
			(surface = lookup_surface(&xwm->unpaired_surfaces, window_id)) ||
			(surface = lookup_surface(&xwm->new_surfaces, window_id))) {
		return surface;
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
	surface->window_id = window_id;
	surface->x = x;
	surface->y = y;
	surface->width = width;
	surface->height = height;
	surface->override_redirect = override_redirect;
	wl_list_insert(&xwm->new_surfaces, &surface->link);
	surface->state = list_create();
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.request_configure);
	wl_signal_init(&surface->events.set_class);
	wl_signal_init(&surface->events.set_title);
	wl_signal_init(&surface->events.set_parent);
	wl_signal_init(&surface->events.set_state);
	wl_signal_init(&surface->events.set_pid);
	wl_signal_init(&surface->events.set_window_type);
	return surface;
}

static void wlr_xwayland_surface_destroy(struct wlr_xwayland_surface *surface) {
	wl_signal_emit(&surface->events.destroy, surface);
	wl_list_remove(&surface->link);
	for (size_t i = 0; i < surface->state->length; i++) {
		free(surface->state->items[i]);
	}
	list_free(surface->state);
	free(surface->window_type);
	free(surface->protocols);
	free(surface->class);
	free(surface->instance);
	free(surface->title);
	free(surface);
}

/* xcb helpers */
#define XCB_CALL(xwm, x) xcb_call(xwm, __func__, __LINE__, x)
static bool xcb_call(struct wlr_xwm *xwm, const char *func, uint32_t line,
		xcb_void_cookie_t cookie) {
	xcb_generic_error_t *error;
	if (!(error = xcb_request_check(xwm->xcb_conn, cookie))) {
		return true;
	}

	wlr_log(L_ERROR, "xcb call failed in %s:%u, x11 error code %d",
		func, line, error->error_code);
	free(error);
	return false;
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

	wlr_log(L_DEBUG, "XCB_ATOM_WM_CLASS: %s %s", surface->instance, surface->class);
	wl_signal_emit(&surface->events.set_class, surface);
}

static void read_surface_title(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_STRING &&
			reply->type != xwm->atoms[UTF8_STRING]) {
		return;
	}

	// TODO: if reply->type == XCB_ATOM_STRING, uses latin1 encoding
	// if reply->type == xwm->atoms[UTF8_STRING], uses utf8 encoding

	size_t len = xcb_get_property_value_length(reply);
	char *title = xcb_get_property_value(reply);

	free(surface->title);
	if (len > 0) {
		surface->title = strndup(title, len);
	} else {
		surface->title = NULL;
	}

	wlr_log(L_DEBUG, "XCB_ATOM_WM_NAME: %s", surface->title);
	wl_signal_emit(&surface->events.set_title, surface);
}

static void read_surface_parent(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_WINDOW) {
		return;
	}

	xcb_window_t *xid = xcb_get_property_value(reply);
	if (xid != NULL) {
		surface->parent = lookup_surface_any(xwm, *xid);
	} else {
		surface->parent = NULL;
	}

	wlr_log(L_DEBUG, "XCB_ATOM_WM_TRANSIENT_FOR: %p", xid);
	wl_signal_emit(&surface->events.set_parent, surface);
}

static void handle_surface_state(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_atom_t *state,
		size_t state_len, enum net_wm_state_action action) {
	for (size_t i = 0; i < state_len; i++) {
		xcb_atom_t atom = state[i];
		bool found = false;
		for (size_t j = 0; j < surface->state->length; j++) {
			xcb_atom_t *cur = surface->state->items[j];
			if (atom == *cur) {
				found = true;
				if (action == NET_WM_STATE_REMOVE ||
						action == NET_WM_STATE_TOGGLE) {
					free(surface->state->items[j]);
					list_del(surface->state, j);
				}
				break;
			}
		}

		if (!found && (action == NET_WM_STATE_ADD ||
				action == NET_WM_STATE_TOGGLE)) {
			xcb_atom_t *atom_ptr = malloc(sizeof(xcb_atom_t));
			*atom_ptr = atom;
			list_add(surface->state, atom_ptr);
		}
	}

	wlr_log(L_DEBUG, "NET_WM_STATE (%zu)", state_len);
	wl_signal_emit(&surface->events.set_state, surface);
}

static void read_surface_state(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	// reply->type == XCB_ATOM_ANY
	handle_surface_state(xwm, surface, xcb_get_property_value(reply),
		reply->value_len, NET_WM_STATE_ADD);
}

static void read_surface_pid(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_CARDINAL) {
		return;
	}

	pid_t *pid = xcb_get_property_value(reply);
	surface->pid = *pid;
	wlr_log(L_DEBUG, "NET_WM_PID %d", surface->pid);
	wl_signal_emit(&surface->events.set_pid, surface);
}

static void read_surface_window_type(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_ATOM) {
		return;
	}

	xcb_atom_t *atoms = xcb_get_property_value(reply);
	size_t atoms_len = reply->value_len;
	size_t atoms_size = sizeof(xcb_atom_t) * atoms_len;

	free(surface->window_type);
	surface->window_type = malloc(atoms_size);
	if (surface->window_type == NULL) {
		return;
	}
	memcpy(surface->window_type, atoms, atoms_size);
	surface->window_type_len = atoms_len;

	wlr_log(L_DEBUG, "NET_WM_WINDOW_TYPE (%zu)", atoms_len);
	wl_signal_emit(&surface->events.set_window_type, surface);
}

static void read_surface_protocols(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != XCB_ATOM_ATOM) {
		return;
	}

	xcb_atom_t *atoms = xcb_get_property_value(reply);
	size_t atoms_len = reply->value_len;
	size_t atoms_size = sizeof(xcb_atom_t) * atoms_len;

	free(surface->protocols);
	surface->protocols = malloc(atoms_size);
	if (surface->protocols == NULL) {
		return;
	}
	memcpy(surface->protocols, atoms, atoms_size);
	surface->protocols_len = atoms_len;

	wlr_log(L_DEBUG, "WM_PROTOCOLS (%zu)", atoms_len);
}

#ifdef HAS_XCB_ICCCM
static void read_surface_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	// According to the docs, reply->type == xwm->atoms[WM_HINTS]
	// In practice, reply->type == XCB_ATOM_ATOM
	if (reply->value_len == 0) {
		return;
	}

	xcb_icccm_get_wm_hints_from_reply(&surface->hints, reply);

	wlr_log(L_DEBUG, "WM_HINTS (%d)", reply->value_len);
}
#else
static void read_surface_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	// Do nothing
}
#endif

#ifdef HAS_XCB_ICCCM
static void read_surface_normal_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->type != xwm->atoms[WM_SIZE_HINTS] || reply->value_len == 0) {
		return;
	}

	xcb_icccm_get_wm_size_hints_from_reply(&surface->size_hints, reply);

	wlr_log(L_DEBUG, "WM_NORMAL_HINTS (%d)", reply->value_len);
}
#else
static void read_surface_normal_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	// Do nothing
}
#endif

static void read_surface_motif_hints(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_get_property_reply_t *reply) {
	if (reply->value_len < 5) {
		return;
	}

	uint32_t *motif_hints = xcb_get_property_value(reply);
	memcpy(surface->motif_hints, motif_hints, sizeof(surface->motif_hints));

	wlr_log(L_DEBUG, "MOTIF_WM_HINTS (%d)", reply->value_len);
}

static void read_surface_property(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *surface, xcb_atom_t property) {
	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn, 0,
		surface->window_id, property, XCB_ATOM_ANY, 0, 2048);
	xcb_get_property_reply_t *reply = xcb_get_property_reply(xwm->xcb_conn,
		cookie, NULL);
	if (reply == NULL) {
		return;
	}

	if (property == XCB_ATOM_WM_CLASS) {
		read_surface_class(xwm, surface, reply);
	} else if (property == XCB_ATOM_WM_NAME ||
			property == xwm->atoms[NET_WM_NAME]) {
		read_surface_title(xwm, surface, reply);
	} else if (property == XCB_ATOM_WM_TRANSIENT_FOR) {
		read_surface_parent(xwm, surface, reply);
	} else if (property == xwm->atoms[NET_WM_PID]) {
		read_surface_pid(xwm, surface, reply);
	} else if (property == xwm->atoms[NET_WM_WINDOW_TYPE]) {
		read_surface_window_type(xwm, surface, reply);
	} else if (property == xwm->atoms[WM_PROTOCOLS]) {
		read_surface_protocols(xwm, surface, reply);
	} else if (property == xwm->atoms[NET_WM_STATE]) {
		read_surface_state(xwm, surface, reply);
	} else if (property == xwm->atoms[WM_HINTS]) {
		read_surface_hints(xwm, surface, reply);
	} else if (property == xwm->atoms[WM_NORMAL_HINTS]) {
		read_surface_normal_hints(xwm, surface, reply);
	} else if (property == xwm->atoms[MOTIF_WM_HINTS]) {
		read_surface_motif_hints(xwm, surface, reply);
	} else {
		wlr_log(L_DEBUG, "unhandled x11 property %u", property);
	}

	free(reply);
}

static void map_shell_surface(struct wlr_xwm *xwm,
		struct wlr_xwayland_surface *xwayland_surface,
		struct wlr_surface *surface) {
	// get xcb geometry for depth = alpha channel
	xwayland_surface->surface = surface;

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
		read_surface_property(xwm, xwayland_surface, props[i]);
	}

	wl_list_remove(&xwayland_surface->link);
	wl_list_insert(&xwm->xwayland->displayable_surfaces,
		&xwayland_surface->link);
	wl_signal_emit(&xwm->xwayland->events.new_surface, xwayland_surface);
}

/* xcb event handlers */
static void handle_create_notify(struct wlr_xwm *xwm,
		xcb_create_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CREATE_NOTIFY (%u)", ev->window);
	wlr_xwayland_surface_create(xwm, ev->window, ev->x, ev->y,
		ev->width, ev->height, ev->override_redirect);
}

static void handle_destroy_notify(struct wlr_xwm *xwm,
		xcb_destroy_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_DESTROY_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *surface = lookup_surface_any(xwm, ev->window);
	if (surface == NULL) {
		return;
	}
	wlr_xwayland_surface_destroy(surface);
}

static void handle_configure_request(struct wlr_xwm *xwm,
		xcb_configure_request_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CONFIGURE_REQUEST (%u) [%ux%u+%d,%d]", ev->window,
		ev->width, ev->height, ev->x, ev->y);
	struct wlr_xwayland_surface *surface = lookup_surface_any(xwm, ev->window);
	if (surface == NULL) {
		return;
	}

	// TODO: handle ev->{parent,sibling}?

	if (surface->surface == NULL) {
		// Surface has not been mapped yet
		wlr_xwayland_surface_configure(xwm->xwayland, surface, ev->x, ev->y,
			ev->width, ev->height);
	} else {
		struct wlr_xwayland_surface_configure_event *wlr_event =
			calloc(1, sizeof(struct wlr_xwayland_surface_configure_event));
		if (wlr_event == NULL) {
			return;
		}

		wlr_event->surface = surface;
		wlr_event->x = ev->x;
		wlr_event->y = ev->y;
		wlr_event->width = ev->width;
		wlr_event->height = ev->height;

		wl_signal_emit(&surface->events.request_configure, wlr_event);

		free(wlr_event);
	}
}

static void handle_map_request(struct wlr_xwm *xwm,
		xcb_map_request_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_MAP_REQUEST (%u)", ev->window);
	const uint32_t value_list = XCB_EVENT_MASK_FOCUS_CHANGE |
		XCB_EVENT_MASK_PROPERTY_CHANGE;
	XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->xcb_conn,
		ev->window, XCB_CW_EVENT_MASK, &value_list));
	XCB_CALL(xwm, xcb_map_window_checked(xwm->xcb_conn, ev->window));
}

static void handle_map_notify(struct wlr_xwm *xwm, xcb_map_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_MAP_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *surface = lookup_surface_any(xwm, ev->window);
	if (surface != NULL) {
		surface->override_redirect = ev->override_redirect;
	} else {
		wlr_xwayland_surface_create(xwm, ev->window, 0, 0, 1, 1,
			ev->override_redirect);
	}
}

static void handle_unmap_notify(struct wlr_xwm *xwm,
		xcb_unmap_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_UNMAP_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *surface = lookup_surface_any(xwm, ev->window);
	if (surface == NULL) {
		return;
	}
	// TODO: remove pointer to surface only?
	wlr_xwayland_surface_destroy(surface);
}

static void handle_property_notify(struct wlr_xwm *xwm,
		xcb_property_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_PROPERTY_NOTIFY (%u)", ev->window);
	struct wlr_xwayland_surface *surface = lookup_surface_any(xwm, ev->window);
	if (surface == NULL) {
		return;
	}

	read_surface_property(xwm, surface, ev->atom);
}

static void handle_client_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CLIENT_MESSAGE (%u)", ev->window);

	if (ev->type == xwm->atoms[WL_SURFACE_ID]) {
		struct wlr_xwayland_surface *surface = lookup_surface(
			&xwm->new_surfaces, ev->window);
		if (surface == NULL) {
			wlr_log(L_DEBUG, "client message WL_SURFACE_ID but no new window %u ?",
				ev->window);
			return;
		}
		surface->surface_id = ev->data.data32[0];
		/* Check if we got notified after wayland surface create event */
		struct wl_resource *resource = wl_client_get_object(
			xwm->xwayland->client, surface->surface_id);
		if (resource) {
			map_shell_surface(xwm, surface, wl_resource_get_user_data(resource));
		} else {
			wl_list_remove(&surface->link);
			wl_list_insert(&xwm->unpaired_surfaces, &surface->link);
		}
	} else if (ev->type == xwm->atoms[NET_WM_STATE]) {
		struct wlr_xwayland_surface *surface = lookup_surface_any(xwm,
			ev->window);
		if (surface == NULL) {
			return;
		}
		handle_surface_state(xwm, surface, &ev->data.data32[1], 2,
			ev->data.data32[0]);
	} else {
		wlr_log(L_DEBUG, "unhandled x11 client message %u", ev->type);
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
		switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
		case XCB_CREATE_NOTIFY:
			handle_create_notify(xwm, (xcb_create_notify_event_t *)event);
			break;
		case XCB_DESTROY_NOTIFY:
			handle_destroy_notify(xwm, (xcb_destroy_notify_event_t *)event);
			break;
		case XCB_CONFIGURE_REQUEST:
			handle_configure_request(xwm, (xcb_configure_request_event_t *)event);
			break;
		case XCB_MAP_REQUEST:
			handle_map_request(xwm, (xcb_map_request_event_t *)event);
			break;
		case XCB_MAP_NOTIFY:
			handle_map_notify(xwm, (xcb_map_notify_event_t *)event);
			break;
		case XCB_UNMAP_NOTIFY:
			handle_unmap_notify(xwm, (xcb_unmap_notify_event_t *)event);
			break;
		case XCB_PROPERTY_NOTIFY:
			handle_property_notify(xwm, (xcb_property_notify_event_t *)event);
			break;
		case XCB_CLIENT_MESSAGE:
			handle_client_message(xwm, (xcb_client_message_event_t *)event);
			break;
		default:
			wlr_log(L_DEBUG, "X11 event: %d",
				event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK);
			break;
		}
		free(event);
	}

	xcb_flush(xwm->xcb_conn);
	return count;
}

static void create_surface_handler(struct wl_listener *listener, void *data) {
	struct wlr_surface *surface = data;
	struct wlr_xwm *xwm = wl_container_of(listener, xwm,
		surface_create_listener);
	if (wl_resource_get_client(surface->resource) != xwm->xwayland->client) {
		return;
	}

	wlr_log(L_DEBUG, "New xwayland surface: %p", surface);

	uint32_t surface_id = wl_resource_get_id(surface->resource);
	struct wlr_xwayland_surface *xwayland_surface;
	wl_list_for_each(xwayland_surface, &xwm->unpaired_surfaces, link) {
		if (xwayland_surface->surface_id == surface_id) {
			map_shell_surface(xwm, xwayland_surface, surface);
			xcb_flush(xwm->xcb_conn);
			return;
		}
	}
}

static void xcb_get_resources(struct wlr_xwm *xwm) {
	size_t i;
	xcb_intern_atom_cookie_t cookies[ATOM_LAST];

	for (i = 0; i < ATOM_LAST; i++) {
		cookies[i] = xcb_intern_atom(xwm->xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
	}
	for (i = 0; i < ATOM_LAST; i++) {
		xcb_intern_atom_reply_t *reply;
		xcb_generic_error_t *error;

		reply = xcb_intern_atom_reply(xwm->xcb_conn, cookies[i], &error);

		if (reply && !error) {
			xwm->atoms[i] = reply->atom;
		}
		if (reply) {
			free(reply);
		}
		if (error) {
			wlr_log(L_ERROR, "could not resolve atom %s, x11 error code %d",
				atom_map[i], error->error_code);
			free(error);
			return;
		}
	}
}

static void xcb_init_wm(struct wlr_xwm *xwm) {
	xcb_screen_iterator_t screen_iterator =
		xcb_setup_roots_iterator(xcb_get_setup(xwm->xcb_conn));
	xwm->screen = screen_iterator.data;

	xwm->window = xcb_generate_id(xwm->xcb_conn);

	uint32_t values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
			XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
			XCB_EVENT_MASK_PROPERTY_CHANGE,
		/* xwm->cursor, */
	};
	XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->xcb_conn,
		xwm->screen->root, XCB_CW_EVENT_MASK /* | XCB_CW_CURSOR */, values));
	XCB_CALL(xwm, xcb_composite_redirect_subwindows_checked(xwm->xcb_conn,
		xwm->screen->root, XCB_COMPOSITE_REDIRECT_MANUAL));

	XCB_CALL(xwm, xcb_create_window_checked(xwm->xcb_conn, XCB_COPY_FROM_PARENT,
		xwm->window, xwm->screen->root, 0, 0, 1, 1, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, xwm->screen->root_visual,
		XCB_CW_EVENT_MASK, (uint32_t[]){XCB_EVENT_MASK_PROPERTY_CHANGE}));
	xcb_atom_t supported[] = {
		xwm->atoms[NET_WM_STATE],
	};
	XCB_CALL(xwm, xcb_change_property_checked(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE, xwm->screen->root, xwm->atoms[NET_SUPPORTED],
		XCB_ATOM_ATOM, 32, sizeof(supported)/sizeof(*supported), supported));

	XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->xcb_conn, xwm->window,
		xwm->atoms[WM_S0], XCB_CURRENT_TIME));
	XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->xcb_conn, xwm->window,
		xwm->atoms[NET_WM_S0], XCB_CURRENT_TIME));
	xcb_flush(xwm->xcb_conn);
}

void wlr_xwayland_surface_activate(struct wlr_xwayland *wlr_xwayland,
		struct wlr_xwayland_surface *surface) {
	struct wlr_xwm *xwm = wlr_xwayland->xwm;
	xcb_client_message_event_t m = {0};
	m.response_type = XCB_CLIENT_MESSAGE;
	m.format = 32;
	m.window = surface->window_id;
	m.type = xwm->atoms[WM_PROTOCOLS];
	m.data.data32[0] = xwm->atoms[WM_TAKE_FOCUS];
	m.data.data32[1] = XCB_TIME_CURRENT_TIME;
	xcb_send_event_checked(xwm->xcb_conn, 0, surface->window_id,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*)&m);
	xcb_set_input_focus_checked(xwm->xcb_conn, XCB_INPUT_FOCUS_POINTER_ROOT,
		surface->window_id, XCB_CURRENT_TIME);
	xcb_configure_window_checked(xwm->xcb_conn, surface->window_id,
		XCB_CONFIG_WINDOW_STACK_MODE, (uint32_t[]){XCB_STACK_MODE_ABOVE});
	xcb_flush(xwm->xcb_conn);
}

void wlr_xwayland_surface_configure(struct wlr_xwayland *wlr_xwayland,
		struct wlr_xwayland_surface *surface, int16_t x, int16_t y,
		uint16_t width, uint16_t height) {
	surface->x = x;
	surface->y = y;
	surface->width = width;
	surface->height = height;

	struct wlr_xwm *xwm = wlr_xwayland->xwm;
	uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
		XCB_CONFIG_WINDOW_BORDER_WIDTH;
	uint32_t values[] = {x, y, width, height, 0};
	xcb_configure_window(xwm->xcb_conn, surface->window_id, mask, values);
}

void wlr_xwayland_surface_close(struct wlr_xwayland *wlr_xwayland,
		struct wlr_xwayland_surface *surface) {
	struct wlr_xwm *xwm = wlr_xwayland->xwm;

	bool supports_delete = false;
	for (size_t i = 0; i < surface->protocols_len; i++) {
		if (surface->protocols[i] == xwm->atoms[WM_DELETE_WINDOW]) {
			supports_delete = true;
			break;
		}
	}

	if (supports_delete) {
		xcb_client_message_event_t ev = {0};
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = surface->window_id;
		ev.format = 32;
		ev.sequence = 0;
		ev.type = xwm->atoms[WM_PROTOCOLS];
		ev.data.data32[0] = xwm->atoms[WM_DELETE_WINDOW];
		ev.data.data32[1] = XCB_CURRENT_TIME;
		XCB_CALL(xwm, xcb_send_event_checked(xwm->xcb_conn, 0,
			surface->window_id, XCB_EVENT_MASK_NO_EVENT, (char *)&ev));
	} else {
		XCB_CALL(xwm, xcb_kill_client_checked(xwm->xcb_conn,
			surface->window_id));
	}
}

void xwm_destroy(struct wlr_xwm *xwm) {
	if (!xwm) {
		return;
	}
	if (xwm->event_source) {
		wl_event_source_remove(xwm->event_source);
	}
	struct wlr_xwayland_surface *surface, *tmp;
	wl_list_for_each_safe(surface, tmp, &xwm->xwayland->displayable_surfaces,
			link) {
		wlr_xwayland_surface_destroy(surface);
	}
	wl_list_for_each_safe(surface, tmp, &xwm->new_surfaces, link) {
		wlr_xwayland_surface_destroy(surface);
	}
	wl_list_for_each_safe(surface, tmp, &xwm->unpaired_surfaces, link) {
		wlr_xwayland_surface_destroy(surface);
	}
	wl_list_remove(&xwm->surface_create_listener.link);
	xcb_disconnect(xwm->xcb_conn);

	free(xwm);
}

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland) {
	struct wlr_xwm *xwm = calloc(1, sizeof(struct wlr_xwm));
	if (xwm == NULL) {
		return NULL;
	}

	xwm->xwayland = wlr_xwayland;
	wl_list_init(&xwm->new_surfaces);
	wl_list_init(&xwm->unpaired_surfaces);

	xwm->xcb_conn = xcb_connect_to_fd(wlr_xwayland->wm_fd[0], NULL);

	int rc = xcb_connection_has_error(xwm->xcb_conn);
	if (rc) {
		wlr_log(L_ERROR, "xcb connect failed: %d", rc);
		free(xwm);
		return NULL;
	}

	struct wl_event_loop *event_loop = wl_display_get_event_loop(
		wlr_xwayland->wl_display);
	xwm->event_source = wl_event_loop_add_fd(event_loop, wlr_xwayland->wm_fd[0],
		WL_EVENT_READABLE, x11_event_handler, xwm);
	// probably not needed
	// wl_event_source_check(xwm->event_source);

	// TODO more xcb init
	// xcb_prefetch_extension_data(xwm->xcb_conn, &xcb_composite_id);
	xcb_get_resources(xwm);
	xcb_init_wm(xwm);

	xwm->surface_create_listener.notify = create_surface_handler;
	wl_signal_add(&wlr_xwayland->compositor->events.create_surface,
		&xwm->surface_create_listener);

	return xwm;
}
