#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <xcb/xfixes.h>
#include <fcntl.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_data_device.h"
#include "wlr/xwm.h"

static const size_t incr_chunk_size = 64 * 1024;

static void xwm_send_selection_notify(struct wlr_xwm *xwm,
		xcb_atom_t property) {
	xcb_selection_notify_event_t selection_notify;

	memset(&selection_notify, 0, sizeof selection_notify);
	selection_notify.response_type = XCB_SELECTION_NOTIFY;
	selection_notify.sequence = 0;
	selection_notify.time = xwm->selection_request.time;
	selection_notify.requestor = xwm->selection_request.requestor;
	selection_notify.selection = xwm->selection_request.selection;
	selection_notify.target = xwm->selection_request.target;
	selection_notify.property = property;

	xcb_send_event(xwm->xcb_conn, 0, // propagate
		xwm->selection_request.requestor,
		XCB_EVENT_MASK_NO_EVENT, (char *)&selection_notify);
}

static int xwm_flush_source_data(struct wlr_xwm *xwm)
{
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_request.requestor,
		xwm->selection_request.property,
		xwm->selection_target,
		8, // format
		xwm->source_data.size,
		xwm->source_data.data);
	xwm->selection_property_set = 1;
	int length = xwm->source_data.size;
	xwm->source_data.size = 0;

	return length;
}

static int xwm_read_data_source(int fd, uint32_t mask, void *data) {
	struct wlr_xwm *xwm = data;
	void *p;

	int current = xwm->source_data.size;
	if (xwm->source_data.size < incr_chunk_size) {
		p = wl_array_add(&xwm->source_data, incr_chunk_size);
	} else {
		p = (char *) xwm->source_data.data + xwm->source_data.size;
	}

	int available = xwm->source_data.alloc - current;

	int len = read(fd, p, available);
	if (len == -1) {
		wlr_log(L_ERROR, "read error from data source: %m");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		wl_event_source_remove(xwm->property_source);
		xwm->property_source = NULL;
		close(fd);
		wl_array_release(&xwm->source_data);
	}

	wlr_log(L_DEBUG, "read %d (available %d, mask 0x%x) bytes: \"%.*s\"",
			len, available, mask, len, (char *) p);

	xwm->source_data.size = current + len;
	if (xwm->source_data.size >= incr_chunk_size) {
		if (!xwm->incr) {
			wlr_log(L_DEBUG, "got %zu bytes, starting incr",
					xwm->source_data.size);
			xwm->incr = 1;
			xcb_change_property(xwm->xcb_conn,
					XCB_PROP_MODE_REPLACE,
					xwm->selection_request.requestor,
					xwm->selection_request.property,
					xwm->atoms[INCR],
					32, /* format */
					1, &incr_chunk_size);
			xwm->selection_property_set = 1;
			xwm->flush_property_on_delete = 1;
			wl_event_source_remove(xwm->property_source);
			xwm->property_source = NULL;
			xwm_send_selection_notify(xwm, xwm->selection_request.property);
		} else if (xwm->selection_property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for "
				"property delete", xwm->source_data.size);

			xwm->flush_property_on_delete = 1;
			wl_event_source_remove(xwm->property_source);
			xwm->property_source = NULL;
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, "
				"property deleted, setting new property",
				xwm->source_data.size);
			xwm_flush_source_data(xwm);
		}
	} else if (len == 0 && !xwm->incr) {
		wlr_log(L_DEBUG, "non-incr transfer complete");
		/* Non-incr transfer all done. */
		xwm_flush_source_data(xwm);
		xwm_send_selection_notify(xwm, xwm->selection_request.property);
		xcb_flush(xwm->xcb_conn);
		wl_event_source_remove(xwm->property_source);
		xwm->property_source = NULL;
		close(fd);
		wl_array_release(&xwm->source_data);
		xwm->selection_request.requestor = XCB_NONE;
	} else if (len == 0 && xwm->incr) {
		wlr_log(L_DEBUG, "incr transfer complete");

		xwm->flush_property_on_delete = 1;
		if (xwm->selection_property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for "
					"property delete", xwm->source_data.size);
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, "
					"property deleted, setting new property",
					xwm->source_data.size);
			xwm_flush_source_data(xwm);
		}
		xcb_flush(xwm->xcb_conn);
		wl_event_source_remove(xwm->property_source);
		xwm->property_source = NULL;
		close(xwm->data_source_fd);
		xwm->data_source_fd = -1;
		close(fd);
	} else {
		wlr_log(L_DEBUG, "nothing happened, buffered the bytes");
	}

	return 1;
}

static void xwm_send_data(struct wlr_xwm *xwm, xcb_atom_t target,
		const char *mime_type) {
	struct wlr_data_source *source;
	int p[2];

	if (pipe(p) == -1) {
		wlr_log(L_ERROR, "pipe failed: %m");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		return;
	}

	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFL, O_NONBLOCK);

	wl_array_init(&xwm->source_data);
	xwm->selection_target = target;
	xwm->data_source_fd = p[0];
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xwm->xwayland->wl_display);
	xwm->property_source = wl_event_loop_add_fd(loop,
		xwm->data_source_fd,
		WL_EVENT_READABLE,
		xwm_read_data_source,
		xwm);

	source = xwm->seat->selection_source;
	source->send(source, mime_type, p[1]);
	close(p[1]);
}

static void xwm_send_timestamp(struct wlr_xwm *xwm) {
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_request.requestor,
		xwm->selection_request.property,
		XCB_ATOM_INTEGER,
		32, // format
		1, &xwm->selection_timestamp);

	xwm_send_selection_notify(xwm, xwm->selection_request.property);
}

static void xwm_send_targets(struct wlr_xwm *xwm) {
	struct wlr_data_source *source = xwm->seat->selection_source;
	size_t n = 2 + source->mime_types.size / sizeof(char *);
	xcb_atom_t *targets = malloc(n * sizeof(xcb_atom_t));
	if (targets == NULL) {
		return;
	}
	targets[0] = xwm->atoms[TIMESTAMP];
	targets[1] = xwm->atoms[TARGETS];

	size_t i = 2;
	char **mime_type_ptr;
	wl_array_for_each(mime_type_ptr, &source->mime_types) {
		char *mime_type = *mime_type_ptr;
		xcb_atom_t atom;
		if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
			atom = xwm->atoms[UTF8_STRING];
		} else if (strcmp(mime_type, "text/plain") == 0) {
			atom = xwm->atoms[TEXT];
		} else {
			xcb_intern_atom_cookie_t cookie =
				xcb_intern_atom(xwm->xcb_conn, 0, strlen(mime_type), mime_type);
			xcb_intern_atom_reply_t *reply =
				xcb_intern_atom_reply(xwm->xcb_conn, cookie, NULL);
			if (reply == NULL) {
				--n;
				continue;
			}
			atom = reply->atom;
			free(reply);
		}
		targets[i] = atom;
		++i;
	}

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_request.requestor,
		xwm->selection_request.property,
		XCB_ATOM_ATOM,
		32, // format
		n, targets);

	xwm_send_selection_notify(xwm, xwm->selection_request.property);

	free(targets);
}

static void xwm_handle_selection_request(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_request_event_t *selection_request =
		(xcb_selection_request_event_t *) event;

	if (selection_request->selection != xwm->atoms[CLIPBOARD]) {
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		return;
	}

	xwm->selection_request = *selection_request;
	xwm->incr = 0;
	xwm->flush_property_on_delete = 0;

	if (selection_request->selection == xwm->atoms[CLIPBOARD_MANAGER]) {
		// The wlroots clipboard should already have grabbed
		// the first target, so just send selection notify
		// now.  This isn't synchronized with the clipboard
		// finishing getting the data, so there's a race here.
		xwm_send_selection_notify(xwm, xwm->selection_request.property);
		return;
	}

	if (xwm->seat->selection_source == NULL) {
		wlr_log(L_DEBUG, "not handling selection request: "
			"no selection source assigned to xwayland seat");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		return;
	}

	// No xwayland surface focused, deny access to clipboard
	if (xwm->focus_surface == NULL) {
		wlr_log(L_DEBUG, "denying read access to clipboard: "
			"no xwayland surface focused");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		return;
	}

	if (selection_request->target == xwm->atoms[TARGETS]) {
		xwm_send_targets(xwm);
	} else if (selection_request->target == xwm->atoms[TIMESTAMP]) {
		xwm_send_timestamp(xwm);
	} else if (selection_request->target == xwm->atoms[UTF8_STRING]) {
		xwm_send_data(xwm, selection_request->target, "text/plain;charset=utf-8");
	} else if (selection_request->target == xwm->atoms[TEXT]) {
		xwm_send_data(xwm, selection_request->target, "text/plain");
	} else {
		xcb_get_atom_name_cookie_t name_cookie =
			xcb_get_atom_name(xwm->xcb_conn, selection_request->target);
		xcb_get_atom_name_reply_t *name_reply =
			xcb_get_atom_name_reply(xwm->xcb_conn, name_cookie, NULL);
		if (name_reply == NULL) {
			wlr_log(L_DEBUG, "not handling selection request: unknown atom");
			xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
			return;
		}
		size_t len = xcb_get_atom_name_name_length(name_reply);
		char *mime_type = malloc((len + 1) * sizeof(char));
		if (mime_type == NULL) {
			free(name_reply);
			return;
		}
		memcpy(mime_type, xcb_get_atom_name_name(name_reply), len);
		mime_type[len] = '\0';
		xwm_send_data(xwm, selection_request->target, mime_type);
		free(mime_type);
		free(name_reply);
	}
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
		wlr_log(L_ERROR, "write error to target fd: %m");
		return 1;
	}

	wlr_log(L_DEBUG, "wrote %d (chunk size %d) of %d bytes",
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
			wlr_log(L_DEBUG, "transfer complete");
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
	struct wl_array mime_types_atoms;
};

static void data_source_accept(struct wlr_data_source *source, uint32_t time,
		const char *mime_type) {
}

static void data_source_send(struct wlr_data_source *base,
		const char *requested_mime_type, int32_t fd) {
	struct x11_data_source *source = (struct x11_data_source *)base;
	struct wlr_xwm *xwm = source->xwm;

	bool found = false;
	xcb_atom_t mime_type_atom;
	char **mime_type_ptr;
	size_t i = 0;
	wl_array_for_each(mime_type_ptr, &source->base.mime_types) {
		char *mime_type = *mime_type_ptr;
		if (strcmp(mime_type, requested_mime_type) == 0) {
			found = true;
			xcb_atom_t *atoms = source->mime_types_atoms.data;
			mime_type_atom = atoms[i];
			break;
		}
		++i;
	}
	if (!found) {
		return;
	}

	xcb_convert_selection(xwm->xcb_conn,
		xwm->selection_window,
		xwm->atoms[CLIPBOARD],
		mime_type_atom,
		xwm->atoms[WL_SELECTION],
		XCB_TIME_CURRENT_TIME);

	xcb_flush(xwm->xcb_conn);

	fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
	xwm->data_source_fd = fd;
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
		4096 // length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL) {
		return;
	}

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
	wl_array_init(&source->mime_types_atoms);
	xcb_atom_t *value = xcb_get_property_value(reply);
	for (uint32_t i = 0; i < reply->value_len; i++) {
		char *mime_type = NULL;

		if (value[i] == xwm->atoms[UTF8_STRING]) {
			mime_type = strdup("text/plain;charset=utf-8");
		} else if (value[i] == xwm->atoms[TEXT]) {
			mime_type = strdup("text/plain");
		} else if (value[i] != xwm->atoms[TARGETS] &&
				value[i] != xwm->atoms[TIMESTAMP]) {
			xcb_get_atom_name_cookie_t name_cookie =
				xcb_get_atom_name(xwm->xcb_conn, value[i]);
			xcb_get_atom_name_reply_t *name_reply =
				xcb_get_atom_name_reply(xwm->xcb_conn, name_cookie, NULL);
			if (name_reply == NULL) {
				continue;
			}
			size_t len = xcb_get_atom_name_name_length(name_reply);
			char *name = xcb_get_atom_name_name(name_reply); // not a C string
			if (memchr(name, '/', len) != NULL) {
				mime_type = malloc((len + 1) * sizeof(char));
				if (mime_type == NULL) {
					free(name_reply);
					continue;
				}
				memcpy(mime_type, name, len);
				mime_type[len] = '\0';
			}
			free(name_reply);
		}

		if (mime_type != NULL) {
			char **mime_type_ptr =
				wl_array_add(&source->base.mime_types, sizeof(*mime_type_ptr));
			if (mime_type_ptr == NULL) {
				break;
			}
			*mime_type_ptr = mime_type;

			xcb_atom_t *atom_ptr =
				wl_array_add(&source->mime_types_atoms, sizeof(*atom_ptr));
			if (atom_ptr == NULL) {
				break;
			}
			*atom_ptr = value[i];
		}
	}

	wlr_seat_set_selection(xwm->seat, &source->base,
		wl_display_next_serial(xwm->xwayland->wl_display));

	free(reply);
}

static void xwm_handle_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_notify_event_t *selection_notify =
		(xcb_selection_notify_event_t *) event;

	if (selection_notify->selection != xwm->atoms[CLIPBOARD]) {
		return;
	}

	// No xwayland surface focused, deny access to clipboard
	if (xwm->focus_surface == NULL) {
		wlr_log(L_DEBUG, "denying write access to clipboard: "
			"no xwayland surface focused");
		return;
	}

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

	if (xfixes_selection_notify->selection != xwm->atoms[CLIPBOARD]) {
		return 0;
	}

	if (xfixes_selection_notify->owner == XCB_WINDOW_NONE) {
		if (xwm->selection_owner != xwm->selection_window) {
			// A real X client selection went away, not our
			// proxy selection
			wlr_seat_set_selection(xwm->seat, NULL,
				wl_display_next_serial(xwm->xwayland->wl_display));
		}

		xwm->selection_owner = XCB_WINDOW_NONE;
		return 1;
	}

	xwm->selection_owner = xfixes_selection_notify->owner;

	// We have to use XCB_TIME_CURRENT_TIME when we claim the
	// selection, so grab the actual timestamp here so we can
	// answer TIMESTAMP conversion requests correctly.
	if (xfixes_selection_notify->owner == xwm->selection_window) {
		xwm->selection_timestamp = xfixes_selection_notify->timestamp;
		wlr_log(L_DEBUG, "our window, skipping");
		return 1;
	}

	xwm->incr = 0;
	// doing this will give a selection notify where we actually handle the sync
	xcb_convert_selection(xwm->xcb_conn, xwm->selection_window,
		xfixes_selection_notify->selection,
		xwm->atoms[TARGETS],
		xwm->atoms[WL_SELECTION],
		xfixes_selection_notify->timestamp);

	xcb_flush(xwm->xcb_conn);

	return 1;
}

int xwm_handle_selection_event(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	if (xwm->seat == NULL) {
		wlr_log(L_DEBUG, "not handling selection events:"
			"no seat assigned to xwayland");
		return 0;
	}

	switch (event->response_type & ~0x80) {
	case XCB_SELECTION_NOTIFY:
		xwm_handle_selection_notify(xwm, event);
		return 1;
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

static void handle_seat_set_selection(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat = data;
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_selection_change);
	struct wlr_data_source *source = seat->selection_source;

	if (source == NULL) {
		if (xwm->selection_owner == xwm->selection_window) {
			xcb_set_selection_owner(xwm->xcb_conn,
				XCB_ATOM_NONE,
				xwm->atoms[CLIPBOARD],
				xwm->selection_timestamp);
		}

		return;
	}

	if (source->send == data_source_send) {
		return;
	}

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->selection_window,
		xwm->atoms[CLIPBOARD],
		XCB_TIME_CURRENT_TIME);
}

void xwm_set_seat(struct wlr_xwm *xwm, struct wlr_seat *seat) {
	assert(xwm);
	assert(seat);
	if (xwm->seat) {
		wl_list_remove(&xwm->seat_selection_change.link);
		xwm->seat = NULL;
	}

	wl_signal_add(&seat->events.selection, &xwm->seat_selection_change);
	xwm->seat_selection_change.notify = handle_seat_set_selection;
	xwm->seat = seat;
	handle_seat_set_selection(&xwm->seat_selection_change, seat);
}
