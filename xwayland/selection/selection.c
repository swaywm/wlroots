#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_gtk_primary_selection.h>
#include <wlr/util/log.h>
#include <xcb/xfixes.h>
#include "xwayland/xwm.h"
#include "xwayland/selection.h"

void xwm_selection_transfer_remove_source(
		struct wlr_xwm_selection_transfer *transfer) {
	if (transfer->source != NULL) {
		wl_event_source_remove(transfer->source);
		transfer->source = NULL;
	}
}

void xwm_selection_transfer_close_source_fd(
		struct wlr_xwm_selection_transfer *transfer) {
	if (transfer->source_fd >= 0) {
		close(transfer->source_fd);
		transfer->source_fd = -1;
	}
}

void xwm_selection_transfer_destroy_property_reply(
		struct wlr_xwm_selection_transfer *transfer) {
	free(transfer->property_reply);
	transfer->property_reply = NULL;
}

xcb_atom_t xwm_mime_type_to_atom(struct wlr_xwm *xwm, char *mime_type) {
	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
		return xwm->atoms[UTF8_STRING];
	} else if (strcmp(mime_type, "text/plain") == 0) {
		return xwm->atoms[TEXT];
	}

	xcb_intern_atom_cookie_t cookie =
		xcb_intern_atom(xwm->xcb_conn, 0, strlen(mime_type), mime_type);
	xcb_intern_atom_reply_t *reply =
		xcb_intern_atom_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL) {
		return XCB_ATOM_NONE;
	}
	xcb_atom_t atom = reply->atom;
	free(reply);
	return atom;
}

char *xwm_mime_type_from_atom(struct wlr_xwm *xwm, xcb_atom_t atom) {
	if (atom == xwm->atoms[UTF8_STRING]) {
		return strdup("text/plain;charset=utf-8");
	} else if (atom == xwm->atoms[TEXT]) {
		return strdup("text/plain");
	} else {
		return xwm_get_atom_name(xwm, atom);
	}
}

struct wlr_xwm_selection *xwm_get_selection(struct wlr_xwm *xwm,
		xcb_atom_t selection_atom) {
	if (selection_atom == xwm->atoms[CLIPBOARD]) {
		return &xwm->clipboard_selection;
	} else if (selection_atom == xwm->atoms[PRIMARY]) {
		return &xwm->primary_selection;
	} else if (selection_atom == xwm->atoms[DND_SELECTION]) {
		return &xwm->dnd_selection;
	} else {
		return NULL;
	}
}

static int xwm_handle_selection_property_notify(struct wlr_xwm *xwm,
		xcb_property_notify_event_t *event) {
	struct wlr_xwm_selection *selections[] = {
		&xwm->clipboard_selection,
		&xwm->primary_selection,
		&xwm->dnd_selection,
	};

	for (size_t i = 0; i < sizeof(selections)/sizeof(selections[0]); ++i) {
		struct wlr_xwm_selection *selection = selections[i];

		if (event->window == selection->window) {
			if (event->state == XCB_PROPERTY_NEW_VALUE &&
					event->atom == xwm->atoms[WL_SELECTION] &&
					selection->incoming.incr) {
				xwm_get_incr_chunk(&selection->incoming);
			}
			return 1;
		}

		struct wlr_xwm_selection_transfer *outgoing;
		wl_list_for_each(outgoing, &selection->outgoing, outgoing_link) {
			if (event->window == outgoing->request.requestor) {
				if (event->state == XCB_PROPERTY_DELETE &&
						event->atom == outgoing->request.property &&
						outgoing->incr) {
					xwm_send_incr_chunk(outgoing);
				}
				return 1;
			}
		}
	}

	return 0;
}

int xwm_handle_selection_event(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	if (xwm->seat == NULL) {
		wlr_log(WLR_DEBUG, "not handling selection events: "
			"no seat assigned to xwayland");
		return 0;
	}

	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
	case XCB_SELECTION_NOTIFY:
		xwm_handle_selection_notify(xwm, (xcb_selection_notify_event_t *)event);
		return 1;
	case XCB_PROPERTY_NOTIFY:
		return xwm_handle_selection_property_notify(xwm,
			(xcb_property_notify_event_t *)event);
	case XCB_SELECTION_REQUEST:
		xwm_handle_selection_request(xwm,
			(xcb_selection_request_event_t *)event);
		return 1;
	}

	switch (event->response_type - xwm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		// an X11 window has copied something to the clipboard
		return xwm_handle_xfixes_selection_notify(xwm,
			(xcb_xfixes_selection_notify_event_t *)event);
	}

	return 0;
}

static void selection_init(struct wlr_xwm *xwm,
		struct wlr_xwm_selection *selection, xcb_atom_t atom) {
	selection->xwm = xwm;
	selection->atom = atom;
	selection->window = xwm->selection_window;
	selection->incoming.selection = selection;
	wl_list_init(&selection->outgoing);

	uint32_t mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(xwm->xcb_conn, selection->window,
		selection->atom, mask);
}

void xwm_selection_init(struct wlr_xwm *xwm) {
	// Clipboard and primary selection
	uint32_t selection_values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE
	};
	xwm->selection_window = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn,
		XCB_COPY_FROM_PARENT,
		xwm->selection_window,
		xwm->screen->root,
		0, 0,
		10, 10,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		xwm->screen->root_visual,
		XCB_CW_EVENT_MASK, selection_values);

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->selection_window,
		xwm->atoms[CLIPBOARD_MANAGER],
		XCB_TIME_CURRENT_TIME);

	selection_init(xwm, &xwm->clipboard_selection, xwm->atoms[CLIPBOARD]);
	selection_init(xwm, &xwm->primary_selection, xwm->atoms[PRIMARY]);

	// Drag'n'drop
	uint32_t dnd_values[] = {
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE
	};
	xwm->dnd_window = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn,
		XCB_COPY_FROM_PARENT,
		xwm->dnd_window,
		xwm->screen->root,
		0, 0,
		8192, 8192,
		0,
		XCB_WINDOW_CLASS_INPUT_ONLY,
		xwm->screen->root_visual,
		XCB_CW_EVENT_MASK, dnd_values);

	uint32_t version = XDND_VERSION;
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->dnd_window,
		xwm->atoms[DND_AWARE],
		XCB_ATOM_ATOM,
		32, // format
		1, &version);

	selection_init(xwm, &xwm->dnd_selection, xwm->atoms[DND_SELECTION]);
}

void xwm_selection_finish(struct wlr_xwm *xwm) {
	if (!xwm) {
		return;
	}
	if (xwm->selection_window) {
		xcb_destroy_window(xwm->xcb_conn, xwm->selection_window);
	}
	if (xwm->dnd_window) {
		xcb_destroy_window(xwm->xcb_conn, xwm->dnd_window);
	}
	if (xwm->seat) {
		if (xwm->seat->selection_source &&
				data_source_is_xwayland(
					xwm->seat->selection_source)) {
			wlr_seat_set_selection(xwm->seat, NULL,
				wl_display_next_serial(xwm->xwayland->wl_display));
		}
		if (xwm->seat->primary_selection_source &&
				primary_selection_source_is_xwayland(
					xwm->seat->primary_selection_source)) {
			wlr_seat_set_gtk_primary_selection(xwm->seat, NULL,
				wl_display_next_serial(xwm->xwayland->wl_display));
		}
		wlr_xwayland_set_seat(xwm->xwayland, NULL);
	}
}

static void xwm_selection_set_owner(struct wlr_xwm_selection *selection,
		bool set) {
	if (set) {
		xcb_set_selection_owner(selection->xwm->xcb_conn,
			selection->window,
			selection->atom,
			XCB_TIME_CURRENT_TIME);
	} else {
		if (selection->owner == selection->window) {
			xcb_set_selection_owner(selection->xwm->xcb_conn,
				XCB_WINDOW_NONE,
				selection->atom,
				selection->timestamp);
		}
	}
}

static void seat_handle_selection(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat = data;
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_selection);
	struct wlr_data_source *source = seat->selection_source;

	if (source != NULL && data_source_is_xwayland(source)) {
		return;
	}

	xwm_selection_set_owner(&xwm->clipboard_selection, source != NULL);
}

static void seat_handle_primary_selection(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat = data;
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_primary_selection);
	struct wlr_gtk_primary_selection_source *source = seat->primary_selection_source;

	if (source != NULL &&
			primary_selection_source_is_xwayland(
				source)) {
		return;
	}

	xwm_selection_set_owner(&xwm->primary_selection, source != NULL);
}

static void seat_handle_start_drag(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_start_drag);
	struct wlr_drag *drag = data;

	xwm_selection_set_owner(&xwm->dnd_selection, drag != NULL);
	xwm_seat_handle_start_drag(xwm, drag);
}

void xwm_set_seat(struct wlr_xwm *xwm, struct wlr_seat *seat) {
	if (xwm->seat != NULL) {
		wl_list_remove(&xwm->seat_selection.link);
		wl_list_remove(&xwm->seat_primary_selection.link);
		wl_list_remove(&xwm->seat_start_drag.link);
		xwm->seat = NULL;
	}

	if (seat == NULL) {
		return;
	}

	xwm->seat = seat;

	wl_signal_add(&seat->events.selection, &xwm->seat_selection);
	xwm->seat_selection.notify = seat_handle_selection;
	wl_signal_add(&seat->events.primary_selection, &xwm->seat_primary_selection);
	xwm->seat_primary_selection.notify = seat_handle_primary_selection;
	wl_signal_add(&seat->events.start_drag, &xwm->seat_start_drag);
	xwm->seat_start_drag.notify = seat_handle_start_drag;

	seat_handle_selection(&xwm->seat_selection, seat);
	seat_handle_primary_selection(&xwm->seat_primary_selection, seat);
}
