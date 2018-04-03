#define _XOPEN_SOURCE 700
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/util/log.h>
#include <xcb/xfixes.h>
#include "xwayland/xwm.h"

static const size_t incr_chunk_size = 64 * 1024;

static xcb_atom_t data_device_manager_dnd_action_to_atom(
		struct wlr_xwm *xwm, enum wl_data_device_manager_dnd_action action) {
	if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) {
		return xwm->atoms[DND_ACTION_COPY];
	} else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) {
		return xwm->atoms[DND_ACTION_MOVE];
	} else if (action & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) {
		return xwm->atoms[DND_ACTION_ASK];
	}
	return XCB_ATOM_NONE;
}

static enum wl_data_device_manager_dnd_action
		data_device_manager_dnd_action_from_atom(struct wlr_xwm *xwm,
		enum atom_name atom) {
	if (atom == xwm->atoms[DND_ACTION_COPY] ||
			atom == xwm->atoms[DND_ACTION_PRIVATE]) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	} else if (atom == xwm->atoms[DND_ACTION_MOVE]) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
	} else if (atom == xwm->atoms[DND_ACTION_ASK]) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
	}
	return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

static void xwm_selection_send_notify(struct wlr_xwm *xwm,
		xcb_selection_request_event_t *req, bool success) {
	xcb_selection_notify_event_t selection_notify = {
		.response_type = XCB_SELECTION_NOTIFY,
		.sequence = 0,
		.time = req->time,
		.requestor = req->requestor,
		.selection = req->selection,
		.target = req->target,
		.property = success ? req->property : XCB_ATOM_NONE,
	};

	wlr_log(L_DEBUG, "SendEvent destination=%d SelectionNotify(31) time=%d "
		"requestor=%d selection=%d target=%d property=%d", req->requestor,
		req->time, req->requestor, req->selection, req->target,
		selection_notify.property);
	xcb_send_event(xwm->xcb_conn,
		0, // propagate
		req->requestor,
		XCB_EVENT_MASK_NO_EVENT,
		(const char *)&selection_notify);
	xcb_flush(xwm->xcb_conn);
}

static int xwm_selection_flush_source_data(
		struct wlr_xwm_selection_transfer *transfer) {
	xcb_change_property(transfer->selection->xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		transfer->request.requestor,
		transfer->request.property,
		transfer->request.target,
		8, // format
		transfer->source_data.size,
		transfer->source_data.data);
	xcb_flush(transfer->selection->xwm->xcb_conn);
	transfer->property_set = true;
	size_t length = transfer->source_data.size;
	transfer->source_data.size = 0;
	return length;
}

static void xwm_selection_transfer_remove_source(
		struct wlr_xwm_selection_transfer *transfer) {
	if (transfer->source != NULL) {
		wl_event_source_remove(transfer->source);
		transfer->source = NULL;
	}
}

static void xwm_selection_transfer_close_source_fd(
		struct wlr_xwm_selection_transfer *transfer) {
	if (transfer->source_fd >= 0) {
		close(transfer->source_fd);
		transfer->source_fd = -1;
	}
}

static void xwm_selection_transfer_start_outgoing(
		struct wlr_xwm_selection_transfer *transfer);

static void xwm_selection_transfer_destroy_outgoing(
		struct wlr_xwm_selection_transfer *transfer) {
	wl_list_remove(&transfer->outgoing_link);

	// Start next queued transfer
	struct wlr_xwm_selection_transfer *first = NULL;
	if (!wl_list_empty(&transfer->selection->outgoing)) {
		first = wl_container_of(transfer->selection->outgoing.prev, first,
			outgoing_link);
		xwm_selection_transfer_start_outgoing(first);
	}

	xwm_selection_transfer_remove_source(transfer);
	xwm_selection_transfer_close_source_fd(transfer);
	wl_array_release(&transfer->source_data);
	free(transfer);
}

static int xwm_data_source_read(int fd, uint32_t mask, void *data) {
	struct wlr_xwm_selection_transfer *transfer = data;
	struct wlr_xwm *xwm = transfer->selection->xwm;

	void *p;
	size_t current = transfer->source_data.size;
	if (transfer->source_data.size < incr_chunk_size) {
		p = wl_array_add(&transfer->source_data, incr_chunk_size);
		if (p == NULL) {
			wlr_log(L_ERROR, "Could not allocate selection source_data");
			goto error_out;
		}
	} else {
		p = (char *)transfer->source_data.data + transfer->source_data.size;
	}

	size_t available = transfer->source_data.alloc - current;
	ssize_t len = read(fd, p, available);
	if (len == -1) {
		wlr_log(L_ERROR, "read error from data source: %m");
		goto error_out;
	}

	wlr_log(L_DEBUG, "read %ld (available %zu, mask 0x%x) bytes: \"%.*s\"",
		len, available, mask, (int)len, (char *)p);

	transfer->source_data.size = current + len;
	if (transfer->source_data.size >= incr_chunk_size) {
		if (!transfer->incr) {
			wlr_log(L_DEBUG, "got %zu bytes, starting incr",
				transfer->source_data.size);

			xcb_change_property(xwm->xcb_conn,
				XCB_PROP_MODE_REPLACE,
				transfer->request.requestor,
				transfer->request.property,
				xwm->atoms[INCR],
				32, /* format */
				1, &incr_chunk_size);
			transfer->incr = true;
			transfer->property_set = true;
			transfer->flush_property_on_delete = true;
			xwm_selection_transfer_remove_source(transfer);
			xwm_selection_send_notify(xwm, &transfer->request, true);
		} else if (transfer->property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for property delete",
				transfer->source_data.size);

			transfer->flush_property_on_delete = true;
			xwm_selection_transfer_remove_source(transfer);
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, property deleted, setting new "
				"property", transfer->source_data.size);
			xwm_selection_flush_source_data(transfer);
		}
	} else if (len == 0 && !transfer->incr) {
		wlr_log(L_DEBUG, "non-incr transfer complete");
		xwm_selection_flush_source_data(transfer);
		xwm_selection_send_notify(xwm, &transfer->request, true);
		xwm_selection_transfer_destroy_outgoing(transfer);
	} else if (len == 0 && transfer->incr) {
		wlr_log(L_DEBUG, "incr transfer complete");

		transfer->flush_property_on_delete = true;
		if (transfer->property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for property delete",
				transfer->source_data.size);
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, property deleted, setting new "
				"property", transfer->source_data.size);
			xwm_selection_flush_source_data(transfer);
		}
		xwm_selection_transfer_destroy_outgoing(transfer);
	} else {
		wlr_log(L_DEBUG, "nothing happened, buffered the bytes");
	}

	return 1;

error_out:
	xwm_selection_send_notify(xwm, &transfer->request, false);
	xwm_selection_transfer_destroy_outgoing(transfer);
	return 0;
}

static void xwm_selection_source_send(struct wlr_xwm_selection *selection,
		const char *mime_type, int32_t fd) {
	if (selection == &selection->xwm->clipboard_selection) {
		struct wlr_data_source *source =
			selection->xwm->seat->selection_source;
		if (source != NULL) {
			wlr_data_source_send(source, mime_type, fd);
			return;
		}
	} else if (selection == &selection->xwm->primary_selection) {
		struct wlr_primary_selection_source *source =
			selection->xwm->seat->primary_selection_source;
		if (source != NULL) {
			source->send(source, mime_type, fd);
			return;
		}
	} else if (selection == &selection->xwm->dnd_selection) {
		struct wlr_data_source *source =
			selection->xwm->seat->drag_source;
		if (source != NULL) {
			wlr_data_source_send(source, mime_type, fd);
			return;
		}
	}

	wlr_log(L_DEBUG, "not sending selection: no selection source available");
}

static struct wl_array *xwm_selection_source_get_mime_types(
		struct wlr_xwm_selection *selection) {
	if (selection == &selection->xwm->clipboard_selection) {
		struct wlr_data_source *source =
			selection->xwm->seat->selection_source;
		if (source != NULL) {
			return &source->mime_types;
		}
	} else if (selection == &selection->xwm->primary_selection) {
		struct wlr_primary_selection_source *source =
			selection->xwm->seat->primary_selection_source;
		if (source != NULL) {
			return &source->mime_types;
		}
	} else if (selection == &selection->xwm->dnd_selection) {
		struct wlr_data_source *source =
			selection->xwm->seat->drag_source;
		if (source != NULL) {
			return &source->mime_types;
		}
	}
	return NULL;
}

static void xwm_selection_transfer_start_outgoing(
		struct wlr_xwm_selection_transfer *transfer) {
	struct wlr_xwm *xwm = transfer->selection->xwm;
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xwm->xwayland->wl_display);
	transfer->source = wl_event_loop_add_fd(loop, transfer->source_fd,
		WL_EVENT_READABLE, xwm_data_source_read, transfer);
}

/**
 * Read the Wayland selection and send it to an Xwayland client.
 */
static void xwm_selection_send_data(struct wlr_xwm_selection *selection,
		xcb_selection_request_event_t *req, const char *mime_type) {
	// Check MIME type
	struct wl_array *mime_types =
		xwm_selection_source_get_mime_types(selection);
	if (mime_types == NULL) {
		wlr_log(L_ERROR, "not sending selection: no MIME type list available");
		xwm_selection_send_notify(selection->xwm, req, false);
		return;
	}

	bool found = false;
	char **mime_type_ptr;
	wl_array_for_each(mime_type_ptr, mime_types) {
		char *t = *mime_type_ptr;
		if (strcmp(t, mime_type) == 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		wlr_log(L_ERROR, "not sending selection: "
			"requested an unsupported MIME type %s", mime_type);
		xwm_selection_send_notify(selection->xwm, req, false);
		return;
	}

	struct wlr_xwm_selection_transfer *transfer =
		calloc(1, sizeof(struct wlr_xwm_selection_transfer));
	if (transfer == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return;
	}
	transfer->selection = selection;
	transfer->request = *req;
	wl_array_init(&transfer->source_data);

	int p[2];
	if (pipe(p) == -1) {
		wlr_log(L_ERROR, "pipe() failed: %m");
		xwm_selection_send_notify(selection->xwm, req, false);
		return;
	}
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFL, O_NONBLOCK);

	transfer->source_fd = p[0];

	wlr_log(L_DEBUG, "Sending Wayland selection %u to Xwayland window with "
		"MIME type %s, target %u", req->target, mime_type, req->target);
	xwm_selection_source_send(selection, mime_type, p[1]);

	wl_list_insert(&selection->outgoing, &transfer->outgoing_link);

	// We can only handle one transfer at a time
	if (wl_list_length(&selection->outgoing) == 1) {
		xwm_selection_transfer_start_outgoing(transfer);
	}
}

static xcb_atom_t xwm_mime_type_to_atom(struct wlr_xwm *xwm, char *mime_type) {
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

static void xwm_dnd_send_event(struct wlr_xwm *xwm, xcb_atom_t type,
		xcb_client_message_data_t *data) {
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_event_t event = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = dest->window_id,
		.type = type,
		.data = *data,
	};

	xcb_send_event(xwm->xcb_conn,
		0, // propagate
		dest->window_id,
		XCB_EVENT_MASK_NO_EVENT,
		(const char *)&event);
	xcb_flush(xwm->xcb_conn);
}

static void xwm_dnd_send_enter(struct wlr_xwm *xwm) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wl_array *mime_types = &drag->source->mime_types;

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_window;
	data.data32[1] = XDND_VERSION << 24;

	// If we have 3 MIME types or less, we can send them directly in the
	// DND_ENTER message
	size_t n = mime_types->size / sizeof(char *);
	if (n <= 3) {
		size_t i = 0;
		char **mime_type_ptr;
		wl_array_for_each(mime_type_ptr, mime_types) {
			char *mime_type = *mime_type_ptr;
			data.data32[2+i] = xwm_mime_type_to_atom(xwm, mime_type);
			++i;
		}
	} else {
		// Let the client know that targets are not contained in the message
		// data and must be retrieved with the DND_TYPE_LIST property
		data.data32[1] |= 1;

		xcb_atom_t targets[n];
		size_t i = 0;
		char **mime_type_ptr;
		wl_array_for_each(mime_type_ptr, mime_types) {
			char *mime_type = *mime_type_ptr;
			targets[i] = xwm_mime_type_to_atom(xwm, mime_type);
			++i;
		}

		xcb_change_property(xwm->xcb_conn,
			XCB_PROP_MODE_REPLACE,
			xwm->dnd_window,
			xwm->atoms[DND_TYPE_LIST],
			XCB_ATOM_ATOM,
			32, // format
			n, targets);
	}

	xwm_dnd_send_event(xwm, xwm->atoms[DND_ENTER], &data);
}

static void xwm_dnd_send_position(struct wlr_xwm *xwm, uint32_t time, int16_t x,
		int16_t y) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_window;
	data.data32[2] = (x << 16) | y;
	data.data32[3] = time;
	data.data32[4] =
		data_device_manager_dnd_action_to_atom(xwm, drag->source->actions);

	xwm_dnd_send_event(xwm, xwm->atoms[DND_POSITION], &data);
}

static void xwm_dnd_send_drop(struct wlr_xwm *xwm, uint32_t time) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_window;
	data.data32[2] = time;

	xwm_dnd_send_event(xwm, xwm->atoms[DND_DROP], &data);
}

static void xwm_dnd_send_leave(struct wlr_xwm *xwm) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_window;

	xwm_dnd_send_event(xwm, xwm->atoms[DND_LEAVE], &data);
}

/*static void xwm_dnd_send_finished(struct wlr_xwm *xwm) {
	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);
	struct wlr_xwayland_surface *dest = xwm->drag_focus;
	assert(dest != NULL);

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_window;
	data.data32[1] = drag->source->accepted;

	if (drag->source->accepted) {
		data.data32[2] = data_device_manager_dnd_action_to_atom(xwm,
			drag->source->current_dnd_action);
	}

	xwm_dnd_send_event(xwm, xwm->atoms[DND_FINISHED], &data);
}*/

static void xwm_selection_send_targets(struct wlr_xwm_selection *selection,
		xcb_selection_request_event_t *req) {
	struct wlr_xwm *xwm = selection->xwm;

	struct wl_array *mime_types =
		xwm_selection_source_get_mime_types(selection);
	if (mime_types == NULL) {
		wlr_log(L_ERROR, "not sending selection targets: "
			"no selection source available");
		xwm_selection_send_notify(selection->xwm, req, false);
		return;
	}

	size_t n = 2 + mime_types->size / sizeof(char *);
	xcb_atom_t targets[n];
	targets[0] = xwm->atoms[TIMESTAMP];
	targets[1] = xwm->atoms[TARGETS];

	size_t i = 0;
	char **mime_type_ptr;
	wl_array_for_each(mime_type_ptr, mime_types) {
		char *mime_type = *mime_type_ptr;
		targets[2+i] = xwm_mime_type_to_atom(xwm, mime_type);
		++i;
	}

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		req->requestor,
		req->property,
		XCB_ATOM_ATOM,
		32, // format
		n, targets);

	xwm_selection_send_notify(selection->xwm, req, true);
}

static void xwm_selection_send_timestamp(struct wlr_xwm_selection *selection,
		xcb_selection_request_event_t *req) {
	xcb_change_property(selection->xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		req->requestor,
		req->property,
		XCB_ATOM_INTEGER,
		32, // format
		1, &selection->timestamp);

	xwm_selection_send_notify(selection->xwm, req, true);
}

static struct wlr_xwm_selection *xwm_get_selection(struct wlr_xwm *xwm,
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

static char *xwm_mime_type_from_atom(struct wlr_xwm *xwm, xcb_atom_t atom) {
	if (atom == xwm->atoms[UTF8_STRING]) {
		return strdup("text/plain;charset=utf-8");
	} else if (atom == xwm->atoms[TEXT]) {
		return strdup("text/plain");
	} else {
		return xwm_get_atom_name(xwm, atom);
	}
}

static void xwm_handle_selection_request(struct wlr_xwm *xwm,
		xcb_selection_request_event_t *req) {
	wlr_log(L_DEBUG, "XCB_SELECTION_REQUEST (time=%u owner=%u, requestor=%u "
		"selection=%u, target=%u, property=%u)",
		req->time, req->owner, req->requestor, req->selection, req->target,
		req->property);

	if (req->selection == xwm->atoms[CLIPBOARD_MANAGER]) {
		// The wlroots clipboard should already have grabbed the first target,
		// so just send selection notify now. This isn't synchronized with the
		// clipboard finishing getting the data, so there's a race here.
		xwm_selection_send_notify(xwm, req, true);
		return;
	}

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, req->selection);
	if (selection == NULL) {
		wlr_log(L_DEBUG, "received selection request for unknown selection");
		return;
	}

	if (selection->window != req->owner) {
		wlr_log(L_DEBUG, "received selection request with invalid owner");
		return;
	}

	// No xwayland surface focused, deny access to clipboard
	if (xwm->focus_surface == NULL && xwm->drag_focus == NULL) {
		char *selection_name = xwm_get_atom_name(xwm, selection->atom);
		wlr_log(L_DEBUG, "denying read access to selection %u (%s): "
			"no xwayland surface focused", selection->atom, selection_name);
		free(selection_name);
		xwm_selection_send_notify(xwm, req, false);
		return;
	}

	if (req->target == xwm->atoms[TARGETS]) {
		xwm_selection_send_targets(selection, req);
	} else if (req->target == xwm->atoms[TIMESTAMP]) {
		xwm_selection_send_timestamp(selection, req);
	} else if (req->target == xwm->atoms[DELETE]) {
		xwm_selection_send_notify(selection->xwm, req, true);
	} else {
		// Send data
		char *mime_type = xwm_mime_type_from_atom(xwm, req->target);
		if (mime_type == NULL) {
			wlr_log(L_ERROR, "ignoring selection request: unknown atom %u",
				req->target);
			xwm_selection_send_notify(xwm, req, false);
			return;
		}
		xwm_selection_send_data(selection, req, mime_type);
		free(mime_type);
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

		if (event->window == xwm->selection_window) {
			if (event->state == XCB_PROPERTY_NEW_VALUE &&
					event->atom == xwm->atoms[WL_SELECTION] &&
					selection->incoming.incr) {
				wlr_log(L_DEBUG, "get incr chunk");
				// TODO
			}
			return 1;
		}

		struct wlr_xwm_selection_transfer *outgoing;
		wl_list_for_each(outgoing, &selection->outgoing, outgoing_link) {
			if (event->window == outgoing->request.requestor) {
				if (event->state == XCB_PROPERTY_DELETE &&
						event->atom == outgoing->request.property &&
						outgoing->incr) {
					wlr_log(L_DEBUG, "send incr chunk");
					// TODO
				}
				return 1;
			}
		}
	}

	return 0;
}

static void xwm_selection_transfer_destroy_property_reply(
		struct wlr_xwm_selection_transfer *transfer) {
	free(transfer->property_reply);
	transfer->property_reply = NULL;
}

/**
 * Write the X11 selection to a Wayland client.
 */
static int xwm_data_source_write(int fd, uint32_t mask, void *data) {
	struct wlr_xwm_selection_transfer *transfer = data;
	struct wlr_xwm *xwm = transfer->selection->xwm;

	char *property = xcb_get_property_value(transfer->property_reply);
	int remainder = xcb_get_property_value_length(transfer->property_reply) -
		transfer->property_start;

	ssize_t len = write(fd, property + transfer->property_start, remainder);
	if (len == -1) {
		xwm_selection_transfer_destroy_property_reply(transfer);
		xwm_selection_transfer_remove_source(transfer);
		xwm_selection_transfer_close_source_fd(transfer);
		wlr_log(L_ERROR, "write error to target fd: %m");
		return 1;
	}

	wlr_log(L_DEBUG, "wrote %ld (chunk size %ld) of %d bytes",
		transfer->property_start + len,
		len, xcb_get_property_value_length(transfer->property_reply));

	transfer->property_start += len;
	if (len == remainder) {
		xwm_selection_transfer_destroy_property_reply(transfer);
		xwm_selection_transfer_remove_source(transfer);

		if (transfer->incr) {
			xcb_delete_property(xwm->xcb_conn, transfer->selection->window,
				xwm->atoms[WL_SELECTION]);
		} else {
			wlr_log(L_DEBUG, "transfer complete");
			xwm_selection_transfer_close_source_fd(transfer);
		}
	}

	return 1;
}

static void xwm_write_property(struct wlr_xwm_selection_transfer *transfer,
		xcb_get_property_reply_t *reply) {
	struct wlr_xwm *xwm = transfer->selection->xwm;

	transfer->property_start = 0;
	transfer->property_reply = reply;

	xwm_data_source_write(transfer->source_fd, WL_EVENT_WRITABLE, transfer);

	if (transfer->property_reply != NULL) {
		struct wl_event_loop *loop =
			wl_display_get_event_loop(xwm->xwayland->wl_display);
		transfer->source = wl_event_loop_add_fd(loop,
			transfer->source_fd, WL_EVENT_WRITABLE, xwm_data_source_write,
			transfer);
	}
}

static void xwm_selection_get_data(struct wlr_xwm_selection *selection) {
	struct wlr_xwm *xwm = selection->xwm;

	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn,
		1, // delete
		selection->window,
		xwm->atoms[WL_SELECTION],
		XCB_GET_PROPERTY_TYPE_ANY,
		0, // offset
		0x1fffffff // length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL) {
		wlr_log(L_ERROR, "Cannot get selection property");
		return;
	}

	struct wlr_xwm_selection_transfer *transfer = &selection->incoming;
	if (reply->type == xwm->atoms[INCR]) {
		transfer->incr = true;
		free(reply);
	} else {
		transfer->incr = false;
		// reply's ownership is transferred to wm, which is responsible
		// for freeing it
		xwm_write_property(transfer, reply);
	}
}

static void source_send(struct wlr_xwm_selection *selection,
		struct wl_array *mime_types, struct wl_array *mime_types_atoms,
		const char *requested_mime_type, int32_t fd) {
	struct wlr_xwm *xwm = selection->xwm;
	struct wlr_xwm_selection_transfer *transfer = &selection->incoming;

	xcb_atom_t *atoms = mime_types_atoms->data;
	bool found = false;
	xcb_atom_t mime_type_atom;
	char **mime_type_ptr;
	size_t i = 0;
	wl_array_for_each(mime_type_ptr, mime_types) {
		char *mime_type = *mime_type_ptr;
		if (strcmp(mime_type, requested_mime_type) == 0) {
			found = true;
			mime_type_atom = atoms[i];
			break;
		}
		++i;
	}
	if (!found) {
		wlr_log(L_DEBUG, "Cannot send X11 selection to Wayland: "
			"unsupported MIME type");
		return;
	}

	xcb_convert_selection(xwm->xcb_conn,
		selection->window,
		selection->atom,
		mime_type_atom,
		xwm->atoms[WL_SELECTION],
		XCB_TIME_CURRENT_TIME);

	xcb_flush(xwm->xcb_conn);

	fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
	transfer->source_fd = fd;
}

struct x11_data_source {
	struct wlr_data_source base;
	struct wlr_xwm_selection *selection;
	struct wl_array mime_types_atoms;
};

static const struct wlr_data_source_impl data_source_impl;

static struct x11_data_source *data_source_from_wlr_data_source(
		struct wlr_data_source *wlr_source) {
	assert(wlr_source->impl == &data_source_impl);
	return (struct x11_data_source *)wlr_source;
}

static void data_source_send(struct wlr_data_source *wlr_source,
		const char *mime_type, int32_t fd) {
	struct x11_data_source *source =
		data_source_from_wlr_data_source(wlr_source);
	struct wlr_xwm_selection *selection = source->selection;

	source_send(selection, &wlr_source->mime_types, &source->mime_types_atoms,
		mime_type, fd);
}

static void data_source_cancel(struct wlr_data_source *wlr_source) {
	struct x11_data_source *source =
		data_source_from_wlr_data_source(wlr_source);
	wlr_data_source_finish(&source->base);
	wl_array_release(&source->mime_types_atoms);
	free(source);
}

static const struct wlr_data_source_impl data_source_impl = {
	.send = data_source_send,
	.cancel = data_source_cancel,
};

struct x11_primary_selection_source {
	struct wlr_primary_selection_source base;
	struct wlr_xwm_selection *selection;
	struct wl_array mime_types_atoms;
};

static void primary_selection_source_send(
		struct wlr_primary_selection_source *base, const char *mime_type,
		int32_t fd) {
	struct x11_primary_selection_source *source =
		(struct x11_primary_selection_source *)base;
	struct wlr_xwm_selection *selection = source->selection;

	source_send(selection, &base->mime_types, &source->mime_types_atoms,
		mime_type, fd);
}

static void primary_selection_source_cancel(
		struct wlr_primary_selection_source *base) {
	struct x11_primary_selection_source *source =
		(struct x11_primary_selection_source *)base;
	wlr_primary_selection_source_finish(&source->base);
	wl_array_release(&source->mime_types_atoms);
	free(source);
}

static bool source_get_targets(struct wlr_xwm_selection *selection,
		struct wl_array *mime_types, struct wl_array *mime_types_atoms) {
	struct wlr_xwm *xwm = selection->xwm;

	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn,
		1, // delete
		selection->window,
		xwm->atoms[WL_SELECTION],
		XCB_GET_PROPERTY_TYPE_ANY,
		0, // offset
		4096 // length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL) {
		return false;
	}

	if (reply->type != XCB_ATOM_ATOM) {
		free(reply);
		return false;
	}

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
				wl_array_add(mime_types, sizeof(*mime_type_ptr));
			if (mime_type_ptr == NULL) {
				break;
			}
			*mime_type_ptr = mime_type;

			xcb_atom_t *atom_ptr =
				wl_array_add(mime_types_atoms, sizeof(*atom_ptr));
			if (atom_ptr == NULL) {
				break;
			}
			*atom_ptr = value[i];
		}
	}

	free(reply);
	return true;
}

static void xwm_selection_get_targets(struct wlr_xwm_selection *selection) {
	// set the wayland selection to the X11 selection
	struct wlr_xwm *xwm = selection->xwm;

	if (selection == &xwm->clipboard_selection) {
		struct x11_data_source *source =
			calloc(1, sizeof(struct x11_data_source));
		if (source == NULL) {
			return;
		}
		wlr_data_source_init(&source->base, &data_source_impl);

		source->selection = selection;
		wl_array_init(&source->mime_types_atoms);

		bool ok = source_get_targets(selection, &source->base.mime_types,
			&source->mime_types_atoms);
		if (ok) {
			wlr_seat_set_selection(xwm->seat, &source->base,
				wl_display_next_serial(xwm->xwayland->wl_display));
		} else {
			wlr_data_source_cancel(&source->base);
		}
	} else if (selection == &xwm->primary_selection) {
		struct x11_primary_selection_source *source =
			calloc(1, sizeof(struct x11_primary_selection_source));
		if (source == NULL) {
			return;
		}
		wlr_primary_selection_source_init(&source->base);
		source->base.send = primary_selection_source_send;
		source->base.cancel = primary_selection_source_cancel;

		source->selection = selection;
		wl_array_init(&source->mime_types_atoms);

		bool ok = source_get_targets(selection, &source->base.mime_types,
			&source->mime_types_atoms);
		if (ok) {
			wlr_seat_set_primary_selection(xwm->seat, &source->base,
				wl_display_next_serial(xwm->xwayland->wl_display));
		} else {
			source->base.cancel(&source->base);
		}
	} else if (selection == &xwm->dnd_selection) {
		// TODO
	}
}

static void xwm_handle_selection_notify(struct wlr_xwm *xwm,
		xcb_selection_notify_event_t *event) {
	wlr_log(L_DEBUG, "XCB_SELECTION_NOTIFY (selection=%u, property=%u, target=%u)",
		event->selection, event->property,
		event->target);

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, event->selection);
	if (selection == NULL) {
		return;
	}

	if (event->property == XCB_ATOM_NONE) {
		wlr_log(L_ERROR, "convert selection failed");
	} else if (event->target == xwm->atoms[TARGETS]) {
		// No xwayland surface focused, deny access to clipboard
		if (xwm->focus_surface == NULL) {
			wlr_log(L_DEBUG, "denying write access to clipboard: "
				"no xwayland surface focused");
			return;
		}

		// This sets the Wayland clipboard (by calling wlr_seat_set_selection)
		xwm_selection_get_targets(selection);
	} else {
		xwm_selection_get_data(selection);
	}
}

static int xwm_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_xfixes_selection_notify_event_t *event) {
	wlr_log(L_DEBUG, "XCB_XFIXES_SELECTION_NOTIFY (selection=%u, owner=%u)",
		event->selection, event->owner);

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, event->selection);
	if (selection == NULL) {
		return 0;
	}

	if (event->owner == XCB_WINDOW_NONE) {
		if (selection->owner != selection->window) {
			// A real X client selection went away, not our
			// proxy selection
			if (selection == &xwm->clipboard_selection) {
				wlr_seat_set_selection(xwm->seat, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
			} else if (selection == &xwm->primary_selection) {
				wlr_seat_set_primary_selection(xwm->seat, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
			} else if (selection == &xwm->dnd_selection) {
				// TODO: DND
			} else {
				wlr_log(L_DEBUG, "X11 selection has been cleared, but cannot "
					"clear Wayland selection");
			}
		}

		selection->owner = XCB_WINDOW_NONE;
		return 1;
	}

	selection->owner = event->owner;

	// We have to use XCB_TIME_CURRENT_TIME when we claim the
	// selection, so grab the actual timestamp here so we can
	// answer TIMESTAMP conversion requests correctly.
	if (event->owner == selection->window) {
		selection->timestamp = event->timestamp;
		return 1;
	}

	struct wlr_xwm_selection_transfer *transfer = &selection->incoming;
	transfer->incr = false;
	// doing this will give a selection notify where we actually handle the sync
	xcb_convert_selection(xwm->xcb_conn, selection->window,
		selection->atom,
		xwm->atoms[TARGETS],
		xwm->atoms[WL_SELECTION],
		event->timestamp);
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

int xwm_handle_selection_client_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	if (ev->type == xwm->atoms[DND_STATUS]) {
		if (xwm->drag == NULL) {
			wlr_log(L_DEBUG, "ignoring XdndStatus client message because "
				"there's no drag");
			return 1;
		}

		xcb_client_message_data_t *data = &ev->data;
		xcb_window_t target_window = data->data32[0];
		bool accepted = data->data32[1] & 1;
		xcb_atom_t action_atom = data->data32[4];

		if (xwm->drag_focus == NULL ||
				target_window != xwm->drag_focus->window_id) {
			wlr_log(L_DEBUG, "ignoring XdndStatus client message because "
				"it doesn't match the current drag focus window ID");
			return 1;
		}

		enum wl_data_device_manager_dnd_action action =
			data_device_manager_dnd_action_from_atom(xwm, action_atom);

		struct wlr_drag *drag = xwm->drag;
		assert(drag != NULL);

		drag->source->accepted = accepted;
		wlr_data_source_dnd_action(drag->source, action);

		wlr_log(L_DEBUG, "DND_STATUS window=%d accepted=%d action=%d",
			target_window, accepted, action);
		return 1;
	} else if (ev->type == xwm->atoms[DND_FINISHED]) {
		// This should only happen after the drag has ended, but before the drag
		// source is destroyed
		if (xwm->seat == NULL || xwm->seat->drag_source == NULL ||
				xwm->drag != NULL) {
			wlr_log(L_DEBUG, "ignoring XdndFinished client message because "
				"there's no finished drag");
			return 1;
		}

		struct wlr_data_source *source = xwm->seat->drag_source;

		xcb_client_message_data_t *data = &ev->data;
		xcb_window_t target_window = data->data32[0];
		bool performed = data->data32[1] & 1;
		xcb_atom_t action_atom = data->data32[2];

		if (xwm->drag_focus == NULL ||
				target_window != xwm->drag_focus->window_id) {
			wlr_log(L_DEBUG, "ignoring XdndFinished client message because "
				"it doesn't match the finished drag focus window ID");
			return 1;
		}

		enum wl_data_device_manager_dnd_action action =
			data_device_manager_dnd_action_from_atom(xwm, action_atom);

		if (performed) {
			wlr_data_source_dnd_finish(source);
		}

		wlr_log(L_DEBUG, "DND_FINISH window=%d performed=%d action=%d",
			target_window, performed, action);
		return 1;
	} else {
		return 0;
	}
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
				xwm->seat->selection_source->impl == &data_source_impl) {
			wlr_seat_set_selection(xwm->seat, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
		}
		if (xwm->seat->primary_selection_source &&
				xwm->seat->primary_selection_source->cancel == primary_selection_source_cancel) {
			wlr_seat_set_primary_selection(xwm->seat, NULL,
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

	if (source != NULL && source->impl == &data_source_impl) {
		return;
	}

	xwm_selection_set_owner(&xwm->clipboard_selection, source != NULL);
}

static void seat_handle_primary_selection(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat = data;
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_primary_selection);
	struct wlr_primary_selection_source *source = seat->primary_selection_source;

	if (source != NULL && source->send == primary_selection_source_send) {
		return;
	}

	xwm_selection_set_owner(&xwm->primary_selection, source != NULL);
}

static void seat_handle_drag_focus(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag = data;
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_focus);

	struct wlr_xwayland_surface *focus = NULL;
	if (drag->focus != NULL) {
		// TODO: check for subsurfaces?
		struct wlr_xwayland_surface *surface;
		wl_list_for_each(surface, &xwm->surfaces, link) {
			if (surface->surface == drag->focus) {
				focus = surface;
				break;
			}
		}
	}

	if (focus == xwm->drag_focus) {
		return;
	}

	if (xwm->drag_focus != NULL) {
		wlr_data_source_dnd_action(drag->source,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
		xwm_dnd_send_leave(xwm);
	}

	xwm->drag_focus = focus;

	if (xwm->drag_focus != NULL) {
		xwm_dnd_send_enter(xwm);
	}
}

static void seat_handle_drag_motion(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_motion);
	struct wlr_drag_motion_event *event = data;
	struct wlr_xwayland_surface *surface = xwm->drag_focus;

	if (surface == NULL) {
		return; // No xwayland surface focused
	}

	xwm_dnd_send_position(xwm, event->time, surface->x + (int16_t)event->sx,
		surface->y + (int16_t)event->sy);
}

static void seat_handle_drag_drop(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_drop);
	struct wlr_drag_drop_event *event = data;

	if (xwm->drag_focus == NULL) {
		return; // No xwayland surface focused
	}

	wlr_log(L_DEBUG, "Wayland drag dropped over an Xwayland window");
	xwm_dnd_send_drop(xwm, event->time);
}

static void seat_handle_drag_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_destroy);

	// Don't reset drag focus yet because the target will read the drag source
	// right after
	if (xwm->drag_focus != NULL && !xwm->drag->source->accepted) {
		wlr_log(L_DEBUG, "Wayland drag cancelled over an Xwayland window");
		xwm_dnd_send_leave(xwm);
	}

	wl_list_remove(&xwm->seat_drag_focus.link);
	wl_list_remove(&xwm->seat_drag_motion.link);
	wl_list_remove(&xwm->seat_drag_drop.link);
	wl_list_remove(&xwm->seat_drag_destroy.link);
	xwm->drag = NULL;
}

static void seat_handle_drag_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_drag_source_destroy);

	wl_list_remove(&xwm->seat_drag_source_destroy.link);
	xwm->drag_focus = NULL;
}

static void seat_handle_start_drag(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag = data;
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_start_drag);

	xwm_selection_set_owner(&xwm->dnd_selection, drag != NULL);
	xwm->drag = drag;
	xwm->drag_focus = NULL;

	if (drag != NULL) {
		wl_signal_add(&drag->events.focus, &xwm->seat_drag_focus);
		xwm->seat_drag_focus.notify = seat_handle_drag_focus;
		wl_signal_add(&drag->events.motion, &xwm->seat_drag_motion);
		xwm->seat_drag_motion.notify = seat_handle_drag_motion;
		wl_signal_add(&drag->events.drop, &xwm->seat_drag_drop);
		xwm->seat_drag_drop.notify = seat_handle_drag_drop;
		wl_signal_add(&drag->events.destroy, &xwm->seat_drag_destroy);
		xwm->seat_drag_destroy.notify = seat_handle_drag_destroy;

		wl_signal_add(&drag->source->events.destroy,
			&xwm->seat_drag_source_destroy);
		xwm->seat_drag_source_destroy.notify = seat_handle_drag_source_destroy;
	}
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
