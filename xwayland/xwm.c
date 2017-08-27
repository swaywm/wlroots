#include <stdlib.h>
#include <xcb/composite.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_surface.h"
#include "wlr/xwayland.h"
#include "xwm.h"

/* General helpers */
// TODO: replace this with hash table?
static struct wlr_x11_window *lookup_window(struct wl_list *list, xcb_window_t window_id) {
	struct wlr_x11_window *window;
	wl_list_for_each(window, list, link) {
		if (window->window_id == window_id) {
			return window;
		}
	}
	return NULL;
}
static struct wlr_x11_window *lookup_window_any(struct wlr_xwm *xwm, xcb_window_t window_id) {
	struct wlr_x11_window *window;
	if ((window = lookup_window(&xwm->xwayland->displayable_windows, window_id)) ||
			(window = lookup_window(&xwm->unpaired_windows, window_id)) ||
			(window = lookup_window(&xwm->new_windows, window_id))) {
		return window;
	}
	return NULL;
}

static struct wlr_x11_window *wlr_x11_window_create(struct wlr_xwm *xwm,
		xcb_window_t window_id, int16_t x, int16_t y,
		uint16_t width, uint16_t height, bool override_redirect) {
	struct wlr_x11_window *window;

	window = calloc(1, sizeof(struct wlr_x11_window));
	if (!window) {
		wlr_log(L_ERROR, "Could not allocate wlr x11 window");
		return NULL;
	}
	window->window_id = window_id;
	window->x = x;
	window->y = y;
	window->width = width;
	window->height = height;
	window->override_redirect = override_redirect;
	wl_list_insert(&xwm->new_windows, &window->link);
	return window;
}

static void wlr_x11_window_destroy(struct wlr_x11_window *window) {
	wl_list_remove(&window->link);
	free(window);
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

static void map_shell_surface(struct wlr_xwm *xwm, struct wlr_x11_window *window,
		struct wlr_surface *surface) {
	// get xcb geometry for depth = alpha channel
	window->surface = surface->resource;

	wl_list_remove(&window->link);
	wl_list_insert(&xwm->xwayland->displayable_windows, &window->link);
}

/* xcb event handlers */
static void handle_create_notify(struct wlr_xwm *xwm, xcb_create_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CREATE_NOTIFY (%u)", ev->window);
	wlr_x11_window_create(xwm, ev->window, ev->x, ev->y,
		ev->width, ev->height, ev->override_redirect);
}

static void handle_destroy_notify(struct wlr_xwm *xwm, xcb_destroy_notify_event_t *ev) {
	struct wlr_x11_window *window;
	wlr_log(L_DEBUG, "XCB_DESTROY_NOTIFY (%u)", ev->window);
	if (!(window = lookup_window_any(xwm, ev->window))) {
		return;
	}
	wlr_x11_window_destroy(window);
}

static void handle_configure_request(struct wlr_xwm *xwm, xcb_configure_request_event_t *ev) {
	struct wlr_x11_window *window;
	wlr_log(L_DEBUG, "XCB_CONFIGURE_REQUEST (%u) [%ux%u+%d,%d]", ev->window,
		ev->width, ev->height, ev->x, ev->y);
	if (!(window = lookup_window_any(xwm, ev->window))) {
		return;
	}

	window->x = ev->x;
	window->y = ev->y;
	window->width = ev->width;
	window->height = ev->height;
	// handle parent/sibling?

	uint32_t values[] = { ev->x, ev->y, ev->width, ev->height, 0 };
	uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
		XCB_CONFIG_WINDOW_BORDER_WIDTH;
	xcb_configure_window(xwm->xcb_conn, ev->window, mask, values);
}

static void handle_map_request(struct wlr_xwm *xwm, xcb_map_request_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_MAP_REQUEST (%u)", ev->window);
	XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->xcb_conn,
			ev->window, XCB_CW_EVENT_MASK,
			&(uint32_t){XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE}));
	XCB_CALL(xwm, xcb_map_window_checked(xwm->xcb_conn, ev->window));
}

static void handle_map_notify(struct wlr_xwm *xwm, xcb_map_notify_event_t *ev) {
	struct wlr_x11_window *window;
	wlr_log(L_DEBUG, "XCB_MAP_NOTIFY (%u)", ev->window);
	if ((window = lookup_window_any(xwm, ev->window))) {
		window->override_redirect = ev->override_redirect;
	} else {
		wlr_x11_window_create(xwm, ev->window, 0, 0, 1, 1, ev->override_redirect);
	}
}

static void handle_unmap_notify(struct wlr_xwm *xwm, xcb_unmap_notify_event_t *ev) {
	struct wlr_x11_window *window;
	wlr_log(L_DEBUG, "XCB_UNMAP_NOTIFY (%u)", ev->window);
	if (!(window = lookup_window_any(xwm, ev->window))) {
		return;
	}
	// remove pointer to surface only?
	wlr_x11_window_destroy(window);
}

static void handle_property_notify(struct wlr_xwm *xwm, xcb_property_notify_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_PROPERTY_NOTIFY (%u)", ev->window);
	// TODO lookup window & get properties
}

static void handle_client_message(struct wlr_xwm *xwm, xcb_client_message_event_t *ev) {
	wlr_log(L_DEBUG, "XCB_CLIENT_MESSAGE (%u)", ev->window);

	if (ev->type == xwm->atoms[WL_SURFACE_ID]) {
		struct wlr_x11_window *window;
		struct wl_resource *resource;
		window = lookup_window(&xwm->new_windows, ev->window);
		if (!window) {
			wlr_log(L_DEBUG, "client message WL_SURFACE_ID but no new window %u ?",
				ev->window);
			return;
		}
		window->surface_id = ev->data.data32[0];
		/* Check if we got notified after wayland surface create event */
		resource = wl_client_get_object(xwm->xwayland->client, window->surface_id);
		if (resource) {
			map_shell_surface(xwm, window, wl_resource_get_user_data(resource));
		} else {
			wl_list_remove(&window->link);
			wl_list_insert(&xwm->unpaired_windows, &window->link);
		}
	}
	wlr_log(L_DEBUG, "unhandled client message %u", ev->type);
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
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, surface_create_listener);
	struct wlr_x11_window *window;
	uint32_t surface_id;

	if (wl_resource_get_client(surface->resource) != xwm->xwayland->client) {
		return;
	}

	wlr_log(L_DEBUG, "New x11 surface: %p", surface);

	surface_id = wl_resource_get_id(surface->resource);
	wl_list_for_each(window, &xwm->unpaired_windows, link) {
		if (window->surface_id == surface_id) {
			map_shell_surface(xwm, window, surface);
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
	xcb_screen_iterator_t screen_iterator;
	screen_iterator = xcb_setup_roots_iterator(xcb_get_setup(xwm->xcb_conn));
	xwm->screen = screen_iterator.data;

	xwm->window = xcb_generate_id(xwm->xcb_conn);

	uint32_t values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | 
			XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
			XCB_EVENT_MASK_PROPERTY_CHANGE,
		/* xwm->cursor, */
	};
	XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->xcb_conn, xwm->screen->root,
			XCB_CW_EVENT_MASK /* | XCB_CW_CURSOR */, values));
	XCB_CALL(xwm, xcb_composite_redirect_subwindows_checked(xwm->xcb_conn,
			xwm->screen->root, XCB_COMPOSITE_REDIRECT_MANUAL));

	XCB_CALL(xwm, xcb_create_window_checked(xwm->xcb_conn, XCB_COPY_FROM_PARENT,
			xwm->window, xwm->screen->root, 0, 0, 1, 1, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, xwm->screen->root_visual,
			XCB_CW_EVENT_MASK, (uint32_t[]){XCB_EVENT_MASK_PROPERTY_CHANGE}));
	xcb_atom_t supported[] = {
		xwm->atoms[NET_WM_STATE],
	};
	XCB_CALL(xwm, xcb_change_property_checked(xwm->xcb_conn, XCB_PROP_MODE_REPLACE,
			xwm->screen->root, xwm->atoms[NET_SUPPORTED], XCB_ATOM_ATOM,
			32, sizeof(supported)/sizeof(*supported), supported));

	XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->xcb_conn, xwm->window,
			xwm->atoms[WM_S0], XCB_CURRENT_TIME));
	XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->xcb_conn, xwm->window,
			xwm->atoms[NET_WM_S0], XCB_CURRENT_TIME));
	xcb_flush(xwm->xcb_conn);
}

void xwm_destroy(struct wlr_xwm *xwm) {
	if (!xwm) {
		return;
	}
	if (xwm->event_source) {
		wl_event_source_remove(xwm->event_source);
	}
	struct wlr_x11_window *window, *tmp;
	wl_list_for_each_safe(window, tmp, &xwm->xwayland->displayable_windows, link) {
		wlr_x11_window_destroy(window);
	}
	wl_list_for_each_safe(window, tmp, &xwm->new_windows, link) {
		wlr_x11_window_destroy(window);
	}
	wl_list_for_each_safe(window, tmp, &xwm->unpaired_windows, link) {
		wlr_x11_window_destroy(window);
	}
	wl_list_remove(&xwm->surface_create_listener.link);
	xcb_disconnect(xwm->xcb_conn);

	free(xwm);
}

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland) {
	struct wlr_xwm *xwm = calloc(1, sizeof(struct wlr_xwm));
	int rc;

	xwm->xwayland = wlr_xwayland;
	wl_list_init(&xwm->new_windows);
	wl_list_init(&xwm->unpaired_windows);

	xwm->xcb_conn = xcb_connect_to_fd(wlr_xwayland->wm_fd[0], NULL);
	if ((rc = xcb_connection_has_error(xwm->xcb_conn))) {
		wlr_log(L_ERROR, "xcb connect failed: %d", rc);
		free(xwm);
		return NULL;
	}

	struct wl_event_loop *event_loop = wl_display_get_event_loop(wlr_xwayland->wl_display);
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
