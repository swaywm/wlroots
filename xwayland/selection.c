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

/*static xcb_atom_t data_device_manager_dnd_action_to_atom(
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
}*/

static void xwm_selection_send_notify(struct wlr_xwm_selection *selection,
		xcb_atom_t property) {
	xcb_selection_notify_event_t selection_notify = {
		.response_type = XCB_SELECTION_NOTIFY,
		.sequence = 0,
		.time = selection->request.time,
		.requestor = selection->request.requestor,
		.selection = selection->request.selection,
		.target = selection->request.target,
		.property = property,
	};

	xcb_send_event(selection->xwm->xcb_conn,
		0, // propagate
		selection->request.requestor,
		XCB_EVENT_MASK_NO_EVENT,
		(char *)&selection_notify);
}

static int xwm_selection_flush_source_data(struct wlr_xwm_selection *selection) {
	xcb_change_property(selection->xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		selection->request.requestor,
		selection->request.property,
		selection->target,
		8, // format
		selection->source_data.size,
		selection->source_data.data);
	selection->property_set = true;
	int length = selection->source_data.size;
	selection->source_data.size = 0;

	return length;
}

static void xwm_data_source_remove_property_source(
		struct wlr_xwm_selection *selection) {
	if (selection->property_source) {
		wl_event_source_remove(selection->property_source);
	}
	selection->property_source = NULL;
}

static void xwm_data_source_close_source_fd(
		struct wlr_xwm_selection *selection) {
	close(selection->source_fd);
	selection->source_fd = -1;
}

static int xwm_data_source_read(int fd, uint32_t mask, void *data) {
	struct wlr_xwm_selection *selection = data;
	struct wlr_xwm *xwm = selection->xwm;

	void *p;
	int current = selection->source_data.size;
	if (selection->source_data.size < incr_chunk_size) {
		p = wl_array_add(&selection->source_data, incr_chunk_size);
		if (!p){
			wlr_log(L_ERROR, "Could not allocate selection source_data");
			goto error_out;
		}
	} else {
		p = (char *) selection->source_data.data + selection->source_data.size;
	}

	int available = selection->source_data.alloc - current;

	int len = read(fd, p, available);
	if (len == -1) {
		wlr_log(L_ERROR, "read error from data source: %m");
		goto error_out;
	}

	wlr_log(L_DEBUG, "read %d (available %d, mask 0x%x) bytes: \"%.*s\"",
		len, available, mask, len, (char *) p);

	selection->source_data.size = current + len;
	if (selection->source_data.size >= incr_chunk_size) {
		if (!selection->incr) {
			wlr_log(L_DEBUG, "got %zu bytes, starting incr",
				selection->source_data.size);
			selection->incr = 1;
			xcb_change_property(xwm->xcb_conn,
					XCB_PROP_MODE_REPLACE,
					selection->request.requestor,
					selection->request.property,
					xwm->atoms[INCR],
					32, /* format */
					1, &incr_chunk_size);
			selection->property_set = true;
			selection->flush_property_on_delete = 1;
			xwm_data_source_remove_property_source(selection);
			xwm_selection_send_notify(selection, selection->request.property);
		} else if (selection->property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for property delete",
				selection->source_data.size);

			selection->flush_property_on_delete = 1;
			xwm_data_source_remove_property_source(selection);
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, property deleted, setting new "
				"property", selection->source_data.size);
			xwm_selection_flush_source_data(selection);
		}
	} else if (len == 0 && !selection->incr) {
		wlr_log(L_DEBUG, "non-incr transfer complete");
		/* Non-incr transfer all done. */
		xwm_selection_flush_source_data(selection);
		xwm_selection_send_notify(selection, selection->request.property);
		xcb_flush(xwm->xcb_conn);
		xwm_data_source_remove_property_source(selection);
		xwm_data_source_close_source_fd(selection);
		wl_array_release(&selection->source_data);
		selection->request.requestor = XCB_NONE;
	} else if (len == 0 && selection->incr) {
		wlr_log(L_DEBUG, "incr transfer complete");

		selection->flush_property_on_delete = 1;
		if (selection->property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for property delete",
				selection->source_data.size);
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, property deleted, setting new "
				"property", selection->source_data.size);
			xwm_selection_flush_source_data(selection);
		}
		xcb_flush(xwm->xcb_conn);
		xwm_data_source_remove_property_source(selection);
		xwm_data_source_close_source_fd(selection);
	} else {
		wlr_log(L_DEBUG, "nothing happened, buffered the bytes");
	}

	return 1;

error_out:
	xwm_selection_send_notify(selection, XCB_ATOM_NONE);
	xwm_data_source_remove_property_source(selection);
	xwm_data_source_close_source_fd(selection);
	wl_array_release(&selection->source_data);
	return 0;
}

static void xwm_selection_source_send(struct wlr_xwm_selection *selection,
		const char *mime_type, int32_t fd) {
	if (selection == &selection->xwm->clipboard_selection) {
		struct wlr_data_source *source =
			selection->xwm->seat->selection_data_source;
		if (source != NULL) {
			source->send(source, mime_type, fd);
			return;
		}
	} else if (selection == &selection->xwm->primary_selection) {
		struct wlr_primary_selection_source *source =
			selection->xwm->seat->primary_selection_source;
		if (source != NULL) {
			source->send(source, mime_type, fd);
			return;
		}
	}

	wlr_log(L_DEBUG, "not sending selection: no selection source available");
}

static void xwm_selection_send_data(struct wlr_xwm_selection *selection,
		xcb_atom_t target, const char *mime_type) {
	int p[2];
	if (pipe(p) == -1) {
		wlr_log(L_ERROR, "pipe failed: %m");
		xwm_selection_send_notify(selection, XCB_ATOM_NONE);
		return;
	}

	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFL, O_NONBLOCK);

	wl_array_init(&selection->source_data);
	selection->target = target;
	selection->source_fd = p[0];
	struct wl_event_loop *loop =
		wl_display_get_event_loop(selection->xwm->xwayland->wl_display);
	selection->property_source = wl_event_loop_add_fd(loop,
		selection->source_fd, WL_EVENT_READABLE, xwm_data_source_read,
		selection);

	xwm_selection_source_send(selection, mime_type, p[1]);
	close(p[1]);
}

static void xwm_selection_send_timestamp(struct wlr_xwm_selection *selection) {
	xcb_change_property(selection->xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		selection->request.requestor,
		selection->request.property,
		XCB_ATOM_INTEGER,
		32, // format
		1, &selection->timestamp);

	xwm_selection_send_notify(selection, selection->request.property);
}

static xcb_atom_t xwm_get_mime_type_atom(struct wlr_xwm *xwm, char *mime_type) {
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

static void xwm_dnd_send_enter(struct wlr_xwm *xwm, struct wlr_drag *drag,
		struct wlr_xwayland_surface *dest) {
	struct wl_array *mime_types = &drag->source->mime_types;

	xcb_client_message_data_t data = { 0 };
	data.data32[0] = xwm->dnd_selection.window;
	data.data32[1] = XDND_VERSION << 24;

	size_t n = mime_types->size / sizeof(char *);
	if (n <= 3) {
		size_t i = 0;
		char **mime_type_ptr;
		wl_array_for_each(mime_type_ptr, mime_types) {
			char *mime_type = *mime_type_ptr;
			data.data32[2+i] = xwm_get_mime_type_atom(xwm, mime_type);
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
			targets[i] = xwm_get_mime_type_atom(xwm, mime_type);
			++i;
		}

		xcb_change_property(xwm->xcb_conn,
			XCB_PROP_MODE_REPLACE,
			xwm->dnd_selection.window,
			xwm->atoms[DND_TYPE_LIST],
			XCB_ATOM_ATOM,
			32, // format
			n, targets);
	}

	xcb_client_message_event_t event = {
		.response_type = XCB_CLIENT_MESSAGE,
		.format = 32,
		.sequence = 0,
		.window = dest->window_id,
		.type = xwm->atoms[DND_ENTER],
		.data = data,
	};

	xcb_send_event(xwm->xcb_conn,
		0, // propagate
		dest->window_id,
		XCB_EVENT_MASK_NO_EVENT,
		(const char *)&event);
}

static struct wl_array *xwm_selection_source_get_mime_types(
		struct wlr_xwm_selection *selection) {
	if (selection == &selection->xwm->clipboard_selection) {
		struct wlr_data_source *source =
			selection->xwm->seat->selection_data_source;
		if (source != NULL) {
			return &source->mime_types;
		}
	} else if (selection == &selection->xwm->primary_selection) {
		struct wlr_primary_selection_source *source =
			selection->xwm->seat->primary_selection_source;
		if (source != NULL) {
			return &source->mime_types;
		}
	}
	return NULL;
}

static void xwm_selection_send_targets(struct wlr_xwm_selection *selection) {
	struct wlr_xwm *xwm = selection->xwm;

	struct wl_array *mime_types = xwm_selection_source_get_mime_types(selection);
	if (mime_types == NULL) {
		wlr_log(L_DEBUG, "not sending selection targets: "
			"no selection source available");
		xwm_selection_send_notify(selection, XCB_ATOM_NONE);
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
		targets[2+i] = xwm_get_mime_type_atom(xwm, mime_type);
		++i;
	}

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		selection->request.requestor,
		selection->request.property,
		XCB_ATOM_ATOM,
		32, // format
		n, targets);

	xwm_selection_send_notify(selection, selection->request.property);
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

static void xwm_handle_selection_request(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_request_event_t *selection_request =
		(xcb_selection_request_event_t *) event;

	wlr_log(L_DEBUG, "XCB_SELECTION_REQUEST (selection=%u, target=%u)",
		selection_request->selection, selection_request->target);

	if (selection_request->selection == xwm->atoms[CLIPBOARD_MANAGER]) {
		// The wlroots clipboard should already have grabbed the first target,
		// so just send selection notify now. This isn't synchronized with the
		// clipboard finishing getting the data, so there's a race here.
		struct wlr_xwm_selection *selection = &xwm->clipboard_selection;
		selection->request = *selection_request;
		selection->incr = 0;
		selection->flush_property_on_delete = 0;
		xwm_selection_send_notify(selection, selection->request.property);
		return;
	}

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, selection_request->selection);
	if (selection == NULL) {
		xwm_selection_send_notify(selection, XCB_ATOM_NONE);
		return;
	}

	selection->request = *selection_request;
	selection->incr = 0;
	selection->flush_property_on_delete = 0;

	// No xwayland surface focused, deny access to clipboard
	if (xwm->focus_surface == NULL) {
		wlr_log(L_DEBUG, "denying read access to clipboard: "
			"no xwayland surface focused");
		xwm_selection_send_notify(selection, XCB_ATOM_NONE);
		return;
	}

	if (selection_request->target == xwm->atoms[TARGETS]) {
		xwm_selection_send_targets(selection);
	} else if (selection_request->target == xwm->atoms[TIMESTAMP]) {
		xwm_selection_send_timestamp(selection);
	} else if (selection_request->target == xwm->atoms[UTF8_STRING]) {
		xwm_selection_send_data(selection, selection_request->target,
			"text/plain;charset=utf-8");
	} else if (selection_request->target == xwm->atoms[TEXT]) {
		xwm_selection_send_data(selection, selection_request->target,
			"text/plain");
	} else {
		xcb_get_atom_name_cookie_t name_cookie =
			xcb_get_atom_name(xwm->xcb_conn, selection_request->target);
		xcb_get_atom_name_reply_t *name_reply =
			xcb_get_atom_name_reply(xwm->xcb_conn, name_cookie, NULL);
		if (name_reply == NULL) {
			wlr_log(L_DEBUG, "not handling selection request: unknown atom");
			xwm_selection_send_notify(selection, XCB_ATOM_NONE);
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
		xwm_selection_send_data(selection, selection_request->target, mime_type);
		free(mime_type);
		free(name_reply);
	}
}

static void xwm_data_source_destroy_property_reply(
		struct wlr_xwm_selection *selection) {
	free(selection->property_reply);
	selection->property_reply = NULL;
}

static int xwm_data_source_write(int fd, uint32_t mask, void *data) {
	struct wlr_xwm_selection *selection = data;
	struct wlr_xwm *xwm = selection->xwm;

	unsigned char *property = xcb_get_property_value(selection->property_reply);
	int remainder = xcb_get_property_value_length(selection->property_reply) -
		selection->property_start;

	int len = write(fd, property + selection->property_start, remainder);
	if (len == -1) {
		xwm_data_source_destroy_property_reply(selection);
		xwm_data_source_remove_property_source(selection);
		xwm_data_source_close_source_fd(selection);
		wlr_log(L_ERROR, "write error to target fd: %m");
		return 1;
	}

	wlr_log(L_DEBUG, "wrote %d (chunk size %d) of %d bytes",
		selection->property_start + len,
		len, xcb_get_property_value_length(selection->property_reply));

	selection->property_start += len;
	if (len == remainder) {
		xwm_data_source_destroy_property_reply(selection);
		xwm_data_source_remove_property_source(selection);

		if (selection->incr) {
			xcb_delete_property(xwm->xcb_conn,
				selection->window,
				xwm->atoms[WL_SELECTION]);
		} else {
			wlr_log(L_DEBUG, "transfer complete");
			xwm_data_source_close_source_fd(selection);
		}
	}

	return 1;
}

static void xwm_write_property(struct wlr_xwm_selection *selection,
		xcb_get_property_reply_t *reply) {
	selection->property_start = 0;
	selection->property_reply = reply;
	xwm_data_source_write(selection->source_fd, WL_EVENT_WRITABLE, selection);

	if (selection->property_reply) {
		struct wl_event_loop *loop =
			wl_display_get_event_loop(selection->xwm->xwayland->wl_display);
		selection->property_source = wl_event_loop_add_fd(loop,
			selection->source_fd, WL_EVENT_WRITABLE, xwm_data_source_write,
			selection);
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
		return;
	}

	if (reply->type == xwm->atoms[INCR]) {
		selection->incr = 1;
		free(reply);
	} else {
		selection->incr = 0;
		// reply's ownership is transferred to wm, which is responsible
		// for freeing it
		xwm_write_property(selection, reply);
	}
}

static void source_send(struct wlr_xwm_selection *selection,
		struct wl_array *mime_types, struct wl_array *mime_types_atoms,
		const char *requested_mime_type, int32_t fd) {
	struct wlr_xwm *xwm = selection->xwm;

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
		wlr_log(L_DEBUG, "cannot send X11 selection: unsupported MIME type");
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
	selection->source_fd = fd;
}

struct x11_data_source {
	struct wlr_data_source base;
	struct wlr_xwm_selection *selection;
	struct wl_array mime_types_atoms;
};

static void data_source_accept(struct wlr_data_source *base, uint32_t time,
		const char *mime_type) {
	// No-op
}

static void data_source_send(struct wlr_data_source *base,
		const char *mime_type, int32_t fd) {
	struct x11_data_source *source = (struct x11_data_source *)base;
	struct wlr_xwm_selection *selection = source->selection;

	source_send(selection, &base->mime_types, &source->mime_types_atoms,
		mime_type, fd);
}

static void data_source_cancel(struct wlr_data_source *base) {
	struct x11_data_source *source = (struct x11_data_source *)base;
	wlr_data_source_finish(&source->base);
	wl_array_release(&source->mime_types_atoms);
	free(source);
}

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
		wlr_data_source_init(&source->base);
		source->base.accept = data_source_accept;
		source->base.send = data_source_send;
		source->base.cancel = data_source_cancel;

		source->selection = selection;
		wl_array_init(&source->mime_types_atoms);

		bool ok = source_get_targets(selection, &source->base.mime_types,
			&source->mime_types_atoms);
		if (ok) {
			wlr_seat_set_selection(xwm->seat, &source->base,
				wl_display_next_serial(xwm->xwayland->wl_display));
		} else {
			source->base.cancel(&source->base);
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
	}
}

static void xwm_handle_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_notify_event_t *selection_notify =
		(xcb_selection_notify_event_t *) event;

	wlr_log(L_DEBUG, "XCB_SELECTION_NOTIFY (selection=%u, property=%u, target=%u)",
		selection_notify->selection, selection_notify->property,
		selection_notify->target);

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, selection_notify->selection);
	if (selection == NULL) {
		return;
	}

	if (selection_notify->property == XCB_ATOM_NONE) {
		wlr_log(L_ERROR, "convert selection failed");
	} else if (selection_notify->target == xwm->atoms[TARGETS]) {
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
		xcb_generic_event_t *event) {
	xcb_xfixes_selection_notify_event_t *xfixes_selection_notify =
		(xcb_xfixes_selection_notify_event_t *)event;

	wlr_log(L_DEBUG, "XCB_XFIXES_SELECTION_NOTIFY (selection=%u, owner=%u)",
		xfixes_selection_notify->selection, xfixes_selection_notify->owner);

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, xfixes_selection_notify->selection);
	if (selection == NULL) {
		return 0;
	}

	if (xfixes_selection_notify->owner == XCB_WINDOW_NONE) {
		if (selection->owner != selection->window) {
			// A real X client selection went away, not our
			// proxy selection
			if (selection == &xwm->clipboard_selection) {
				wlr_seat_set_selection(xwm->seat, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
			} else if (selection == &xwm->primary_selection) {
				wlr_seat_set_primary_selection(xwm->seat, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
			}
		}

		selection->owner = XCB_WINDOW_NONE;
		return 1;
	}

	selection->owner = xfixes_selection_notify->owner;

	// We have to use XCB_TIME_CURRENT_TIME when we claim the
	// selection, so grab the actual timestamp here so we can
	// answer TIMESTAMP conversion requests correctly.
	if (xfixes_selection_notify->owner == selection->window) {
		selection->timestamp = xfixes_selection_notify->timestamp;
		return 1;
	}

	selection->incr = 0;
	// doing this will give a selection notify where we actually handle the sync
	xcb_convert_selection(xwm->xcb_conn, selection->window,
		selection->atom,
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

static void selection_init(struct wlr_xwm *xwm,
		struct wlr_xwm_selection *selection, xcb_atom_t atom) {
	selection->xwm = xwm;
	selection->atom = atom;
	selection->window = xwm->selection_window;
	selection->request.requestor = XCB_NONE;

	uint32_t mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(xwm->xcb_conn, selection->window,
		selection->atom, mask);
}

void xwm_selection_init(struct wlr_xwm *xwm) {
	// Clipboard and primary selection
	uint32_t selection_values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
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
	uint32_t version = XDND_VERSION;
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_window,
		xwm->atoms[DND_AWARE],
		XCB_ATOM_ATOM,
		32,
		1,
		&version);

	selection_init(xwm, &xwm->dnd_selection, xwm->atoms[DND_SELECTION]);
	wlr_log(L_DEBUG, "DND_SELECTION=%d", xwm->atoms[DND_SELECTION]);
}

void xwm_selection_finish(struct wlr_xwm *xwm) {
	if (!xwm) {
		return;
	}
	if (xwm->selection_window) {
		xcb_destroy_window(xwm->xcb_conn, xwm->selection_window);
	}
	if (xwm->seat) {
		if (xwm->seat->selection_data_source &&
				xwm->seat->selection_data_source->cancel == data_source_cancel) {
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
	struct wlr_data_source *source = seat->selection_data_source;

	if (source != NULL && source->send == data_source_send) {
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

	// TODO: check for subsurfaces?
	bool found = false;
	struct wlr_xwayland_surface *surface;
	wl_list_for_each(surface, &xwm->surfaces, link) {
		if (surface->surface == drag->focus) {
			found = true;
			break;
		}
	}
	if (!found) {
		return;
	}

	xwm_dnd_send_enter(xwm, drag, surface);
}

static void seat_handle_drag_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_drag_destroy);

	wl_list_remove(&xwm->seat_drag_focus.link);
	wl_list_remove(&xwm->seat_drag_destroy.link);
}

static void seat_handle_start_drag(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag = data;
	struct wlr_xwm *xwm = wl_container_of(listener, xwm, seat_start_drag);

	xwm_selection_set_owner(&xwm->dnd_selection, drag != NULL);

	wl_signal_add(&drag->events.focus, &xwm->seat_drag_focus);
	xwm->seat_drag_focus.notify = seat_handle_drag_focus;
	wl_signal_add(&drag->events.destroy, &xwm->seat_drag_destroy);
	xwm->seat_drag_destroy.notify = seat_handle_drag_destroy;
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
