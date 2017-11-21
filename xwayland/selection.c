#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <xcb/xfixes.h>
#include <fcntl.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_data_device.h"
#include "xwm.h"

static int xwm_handle_selection_property_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_property_notify_event_t *property_notify =
		(xcb_property_notify_event_t *) event;

	if (property_notify->window == xwm->selection_window) {
		if (property_notify->state == XCB_PROPERTY_NEW_VALUE &&
				property_notify->atom == xwm->atoms[WL_SELECTION] &&
				xwm->incr)
			wlr_log(L_DEBUG, "TODO: get selection");
			return 1;
	} else if (property_notify->window == xwm->selection_request.requestor) {
		if (property_notify->state == XCB_PROPERTY_DELETE &&
				property_notify->atom == xwm->selection_request.property &&
				xwm->incr)
			wlr_log(L_DEBUG, "TODO: send selection");
			return 1;
	}

	return 0;
}

static void xwm_handle_selection_request(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	wlr_log(L_DEBUG, "TODO: SELECTION REQUEST");
	return;
}

static int writable_callback(int fd, uint32_t mask, void *data) {
	struct wlr_xwm *xwm = data;

	unsigned char *property = xcb_get_property_value(xwm->property_reply);
	int remainder = xcb_get_property_value_length(xwm->property_reply) -
		xwm->property_start;

	int len = write(fd, property + xwm->property_start, remainder);
	if (len == -1) {
		free(xwm->property_reply);
		xwm->property_reply = NULL;
		if (xwm->property_source) {
			wl_event_source_remove(xwm->property_source);
		}
		xwm->property_source = NULL;
		close(fd);
		wlr_log(L_ERROR, "write error to target fd: %m\n");
		return 1;
	}

	wlr_log(L_DEBUG, "wrote %d (chunk size %d) of %d bytes\n",
		xwm->property_start + len,
		len, xcb_get_property_value_length(xwm->property_reply));

	xwm->property_start += len;
	if (len == remainder) {
		free(xwm->property_reply);
		xwm->property_reply = NULL;
		if (xwm->property_source) {
			wl_event_source_remove(xwm->property_source);
		}
		xwm->property_source = NULL;

		if (xwm->incr) {
			xcb_delete_property(xwm->xcb_conn,
				xwm->selection_window,
				xwm->atoms[WL_SELECTION]);
		} else {
			wlr_log(L_DEBUG, "transfer complete\n");
			close(fd);
		}
	}

	return 1;
}

static void xwm_write_property(struct wlr_xwm *xwm,
		xcb_get_property_reply_t *reply) {
	xwm->property_start = 0;
	xwm->property_reply = reply;
	writable_callback(xwm->data_source_fd, WL_EVENT_WRITABLE, xwm);

	if (xwm->property_reply) {
		struct wl_event_loop *loop =
			wl_display_get_event_loop(xwm->xwayland->wl_display);
		xwm->property_source =
			wl_event_loop_add_fd(loop,
				xwm->data_source_fd,
				WL_EVENT_WRITABLE,
				writable_callback, xwm);
	}
}

static void xwm_get_selection_data(struct wlr_xwm *xwm) {
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
			1, // delete
			xwm->selection_window,
			xwm->atoms[WL_SELECTION],
			XCB_GET_PROPERTY_TYPE_ANY,
			0, // offset
			0x1fffffff // length
			);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);

	if (reply == NULL) {
		return;
	} else if (reply->type == xwm->atoms[INCR]) {
		xwm->incr = 1;
		free(reply);
	} else {
		xwm->incr = 0;
		// reply's ownership is transferred to wm, which is responsible
		// for freeing it
		xwm_write_property(xwm, reply);
	}

}

struct x11_data_source {
	struct wlr_data_source base;
	struct wlr_xwm *xwm;
};

static void data_source_accept(struct wlr_data_source *source, uint32_t time,
		const char *mime_type) {
}

static void data_source_send(struct wlr_data_source *base,
		const char *mime_type, int32_t fd) {
	struct x11_data_source *source = (struct x11_data_source *)base;
	struct wlr_xwm *xwm = source->xwm;

	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
		// Get data for the utf8_string target
		xcb_convert_selection(xwm->xcb_conn,
			xwm->selection_window,
			xwm->atoms[CLIPBOARD],
			xwm->atoms[UTF8_STRING],
			xwm->atoms[WL_SELECTION],
			XCB_TIME_CURRENT_TIME);

		xcb_flush(xwm->xcb_conn);

		fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
		xwm->data_source_fd = fd;
	}
}

static void data_source_cancel(struct wlr_data_source *source) {
}

static void xwm_get_selection_targets(struct wlr_xwm *xwm) {
	// set the wayland clipboard selection to the copied selection

	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn,
		1, // delete
		xwm->selection_window,
		xwm->atoms[WL_SELECTION],
		XCB_GET_PROPERTY_TYPE_ANY,
		0, // offset
		4096 //length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL)
		return;

	if (reply->type != XCB_ATOM_ATOM) {
		free(reply);
		return;
	}

	struct x11_data_source *source = calloc(1, sizeof(struct x11_data_source));
	if (source == NULL) {
		free(reply);
		return;
	}

	wl_signal_init(&source->base.events.destroy);
	source->base.accept = data_source_accept;
	source->base.send = data_source_send;
	source->base.cancel = data_source_cancel;
	source->xwm = xwm;

	wl_array_init(&source->base.mime_types);
	xcb_atom_t *value = xcb_get_property_value(reply);
	for (uint32_t i = 0; i < reply->value_len; i++) {
		if (value[i] == xwm->atoms[UTF8_STRING]) {
			char **p = wl_array_add(&source->base.mime_types, sizeof *p);
			if (p) {
				*p = strdup("text/plain;charset=utf-8");
			}
		}
	}

	wlr_seat_set_selection(xwm->xwayland->seat, &source->base,
		wl_display_next_serial(xwm->xwayland->wl_display));

	free(reply);

}

static void xwm_handle_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_notify_event_t *selection_notify =
		(xcb_selection_notify_event_t *) event;

	if (selection_notify->property == XCB_ATOM_NONE) {
		wlr_log(L_ERROR, "convert selection failed");
	} else if (selection_notify->target == xwm->atoms[TARGETS]) {
		xwm_get_selection_targets(xwm);
	} else {
		xwm_get_selection_data(xwm);
	}
}

static int xwm_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_xfixes_selection_notify_event_t *xfixes_selection_notify =
		(xcb_xfixes_selection_notify_event_t *) event;


	if (xfixes_selection_notify->owner == XCB_WINDOW_NONE) {
		if (xwm->selection_owner != xwm->selection_window) {
			// A real X client selection went away, not our
			// proxy selection
			// TODO: Clear the wayland selection (or not)?
		}

		xwm->selection_owner = XCB_WINDOW_NONE;

		return 1;
	}

	// We have to use XCB_TIME_CURRENT_TIME when we claim the
	// selection, so grab the actual timestamp here so we can
	// answer TIMESTAMP conversion requests correctly.
	if (xfixes_selection_notify->owner == xwm->selection_window) {
		xwm->selection_timestamp = xfixes_selection_notify->timestamp;
		wlr_log(L_DEBUG, "TODO: our window");
		return 1;
	}

	xwm->selection_owner = xfixes_selection_notify->owner;

	xwm->incr = 0;
	// doing this will give a selection notify where we actually handle the sync
	xcb_convert_selection(xwm->xcb_conn, xwm->selection_window,
		xwm->atoms[CLIPBOARD],
		xwm->atoms[TARGETS],
		xwm->atoms[WL_SELECTION],
		xfixes_selection_notify->timestamp);

	xcb_flush(xwm->xcb_conn);

	return 1;
}


int xwm_handle_selection_event(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	if (!xwm->xwayland->seat) {
		wlr_log(L_DEBUG, "not handling selection events:"
			"no seat assigned to xwayland");
		return 0;
	}

	switch (event->response_type & ~0x80) {
	case XCB_SELECTION_NOTIFY:
		xwm_handle_selection_notify(xwm, event);
		return 1;
	case XCB_PROPERTY_NOTIFY:
		return xwm_handle_selection_property_notify(xwm, event);
	case XCB_SELECTION_REQUEST:
		xwm_handle_selection_request(xwm, event);
		return 1;
	}

	switch (event->response_type - xwm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		// an X11 window has copied something to the clipboard
		return xwm_handle_xfixes_selection_notify(xwm, event);
	}

	return 0;
}

void xwm_selection_init(struct wlr_xwm *xwm) {
	uint32_t values[1], mask;

	xwm->selection_request.requestor = XCB_NONE;

	values[0] = XCB_EVENT_MASK_PROPERTY_CHANGE;
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
		XCB_CW_EVENT_MASK, values);

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->selection_window,
		xwm->atoms[CLIPBOARD_MANAGER],
		XCB_TIME_CURRENT_TIME);

	mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(xwm->xcb_conn, xwm->selection_window,
		xwm->atoms[CLIPBOARD], mask);
}
