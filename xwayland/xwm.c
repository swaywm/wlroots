#include <stdlib.h>
#include <xcb/xcb_event.h>
#include <xcb/composite.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_surface.h"
#include "wlr/xwayland.h"
#include "xwayland/internals.h"




static int x11_event_handler(int fd, uint32_t mask, void *data) {
	int count = 0;
	xcb_generic_event_t *event;
	struct wlr_xwayland *wlr_xwayland = data;
	struct wlr_xwm *xwm = wlr_xwayland->xwm;

	while ((event = xcb_poll_for_event(xwm->xcb_conn))) {
		count++;
		switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
		case XCB_CREATE_NOTIFY:
			break;
		case XCB_CONFIGURE_REQUEST:
			break;
		case XCB_MAP_REQUEST:
			break;
		case XCB_DESTROY_NOTIFY:
			break;
		default:
			wlr_log(L_DEBUG, "X11 event: %d", event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK);
		}
	}
	return count;
}

static void create_surface_handler(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
        struct wlr_xwm *xwm = wl_container_of(listener, xwm, surface_listener);

	if (wl_resource_get_client(surface->resource) != xwm->xwayland->client) {
		return;
	}

	wlr_log(L_DEBUG, "new x11 surface: %p", surface);

	// TODO: look for unpaired window, and assign
}

static void xcb_get_resources(struct wlr_xwm *xwm) {
	int i;

	for (i = 0; i < ATOM_LAST; i++) {
		xcb_intern_atom_cookie_t cookie;
		xcb_intern_atom_reply_t *reply;
		xcb_generic_error_t *error;

		cookie = xcb_intern_atom(xwm->xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
		reply = xcb_intern_atom_reply(xwm->xcb_conn, cookie, &error);

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

static bool xcb_call(struct wlr_xwm *xwm, const char *func, uint32_t line, xcb_void_cookie_t cookie) {
	xcb_generic_error_t *error;
	if (!(error = xcb_request_check(xwm->xcb_conn, cookie))) {
		return true;
	}

	wlr_log(L_ERROR, "xcb call failed in %s:%u, x11 error code %d", func, line, error->error_code);
	free(error);
	return false;
}
#define XCB_CALL(xwm, x) xcb_call(xwm, __PRETTY_FUNCTION__, __LINE__, x)

static void xcb_init_wm(struct wlr_xwm *xwm) {
	xcb_screen_iterator_t screen_iterator;
	screen_iterator = xcb_setup_roots_iterator(xcb_get_setup(xwm->xcb_conn));
	xwm->screen = screen_iterator.data;

	xwm->window = xcb_generate_id(xwm->xcb_conn);

	uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE /* , xwm->cursor */ };
	XCB_CALL(xwm, xcb_change_window_attributes_checked(xwm->xcb_conn, xwm->screen->root,
				XCB_CW_EVENT_MASK /* | XCB_CW_CURSOR */, values));
	XCB_CALL(xwm, xcb_composite_redirect_subwindows_checked(xwm->xcb_conn, xwm->screen->root, XCB_COMPOSITE_REDIRECT_MANUAL));

	XCB_CALL(xwm, xcb_create_window_checked(xwm->xcb_conn, XCB_COPY_FROM_PARENT, xwm->window, xwm->screen->root,
				0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, xwm->screen->root_visual,
				XCB_CW_EVENT_MASK, (uint32_t[]){XCB_EVENT_MASK_PROPERTY_CHANGE}));
	xcb_atom_t supported[] = {
		xwm->atoms[NET_WM_STATE],
	};
	XCB_CALL(xwm, xcb_change_property_checked(xwm->xcb_conn, XCB_PROP_MODE_REPLACE, xwm->screen->root, xwm->atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, sizeof(supported)/sizeof(*supported), supported));

	XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->xcb_conn, xwm->window, xwm->atoms[WM_S0], XCB_CURRENT_TIME));
	XCB_CALL(xwm, xcb_set_selection_owner_checked(xwm->xcb_conn, xwm->window, xwm->atoms[NET_WM_S0], XCB_CURRENT_TIME));
	xcb_flush(xwm->xcb_conn);
}

void xwm_destroy(struct wlr_xwm *xwm) {
	if (xwm->event_source) {
		wl_event_source_remove(xwm->event_source);
	}

	xcb_disconnect(xwm->xcb_conn);

	free(xwm);
}

struct wlr_xwm *xwm_create(struct wlr_xwayland *wlr_xwayland) {
	struct wlr_xwm *xwm = calloc(1, sizeof(struct wlr_xwm));

	xwm->xwayland = wlr_xwayland;

	xwm->xcb_conn = xcb_connect_to_fd(wlr_xwayland->wm_fd[0], NULL);
	if (xcb_connection_has_error(xwm->xcb_conn)) {
		free(xwm);
		return NULL;
	}


        struct wl_event_loop *event_loop = wl_display_get_event_loop(wlr_xwayland->wl_display);
	xwm->event_source = wl_event_loop_add_fd(event_loop, wlr_xwayland->wm_fd[0],
			WL_EVENT_READABLE, x11_event_handler, wlr_xwayland);
	// probably not needed
	// wl_event_source_check(xwm->event_source);

	// TODO more xcb init
	// xcb_prefetch_extension_data(xwm->xcb_conn, &xcb_composite_id);
	xcb_get_resources(xwm);
	xcb_init_wm(xwm);

	xwm->surface_listener.notify = create_surface_handler;
	wl_signal_add(&wlr_xwayland->compositor->create_surface_signal,
			&xwm->surface_listener);

	return xwm;
}
