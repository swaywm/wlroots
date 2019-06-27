#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/util/log.h>
#include <xcb/xfixes.h>
#include "xwayland/selection.h"
#include "xwayland/xwm.h"

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
		wlr_log(WLR_ERROR, "write error to target fd: %m");
		return 1;
	}

	wlr_log(WLR_DEBUG, "wrote %zd (chunk size %zd) of %d bytes",
		transfer->property_start + len,
		len, xcb_get_property_value_length(transfer->property_reply));

	transfer->property_start += len;
	if (len == remainder) {
		xwm_selection_transfer_destroy_property_reply(transfer);
		xwm_selection_transfer_remove_source(transfer);

		if (transfer->incr) {
			wlr_log(WLR_DEBUG, "deleting property");
			xcb_delete_property(xwm->xcb_conn, transfer->selection->window,
				xwm->atoms[WL_SELECTION]);
			xcb_flush(xwm->xcb_conn);
		} else {
			wlr_log(WLR_DEBUG, "transfer complete");
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

void xwm_get_incr_chunk(struct wlr_xwm_selection_transfer *transfer) {
	struct wlr_xwm *xwm = transfer->selection->xwm;
	wlr_log(WLR_DEBUG, "xwm_get_incr_chunk");

	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn,
		0, // delete
		transfer->selection->window,
		xwm->atoms[WL_SELECTION],
		XCB_GET_PROPERTY_TYPE_ANY,
		0, // offset
		0x1fffffff // length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL) {
		wlr_log(WLR_ERROR, "cannot get selection property");
		return;
	}
	//dump_property(xwm, xwm->atoms[WL_SELECTION], reply);

	if (xcb_get_property_value_length(reply) > 0) {
		/* Reply's ownership is transferred to xwm, which is responsible
		 * for freeing it */
		xwm_write_property(transfer, reply);
	} else {
		wlr_log(WLR_DEBUG, "transfer complete");
		xwm_selection_transfer_close_source_fd(transfer);
		free(reply);
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
		wlr_log(WLR_ERROR, "Cannot get selection property");
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
		const char *requested_mime_type, int fd) {
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
		wlr_log(WLR_DEBUG, "Cannot send X11 selection to Wayland: "
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

bool data_source_is_xwayland(
		struct wlr_data_source *wlr_source) {
	return wlr_source->impl == &data_source_impl;
}

static struct x11_data_source *data_source_from_wlr_data_source(
		struct wlr_data_source *wlr_source) {
	assert(data_source_is_xwayland(wlr_source));
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

static void data_source_destroy(struct wlr_data_source *wlr_source) {
	struct x11_data_source *source =
		data_source_from_wlr_data_source(wlr_source);
	wl_array_release(&source->mime_types_atoms);
	free(source);
}

static const struct wlr_data_source_impl data_source_impl = {
	.send = data_source_send,
	.destroy = data_source_destroy,
};

struct x11_primary_selection_source {
	struct wlr_primary_selection_source base;
	struct wlr_xwm_selection *selection;
	struct wl_array mime_types_atoms;
};

static const struct wlr_primary_selection_source_impl
	primary_selection_source_impl;

bool primary_selection_source_is_xwayland(
		struct wlr_primary_selection_source *wlr_source) {
	return wlr_source->impl == &primary_selection_source_impl;
}

static void primary_selection_source_send(
		struct wlr_primary_selection_source *wlr_source,
		const char *mime_type, int fd) {
	struct x11_primary_selection_source *source =
		(struct x11_primary_selection_source *)wlr_source;
	struct wlr_xwm_selection *selection = source->selection;

	source_send(selection, &wlr_source->mime_types, &source->mime_types_atoms,
		mime_type, fd);
}

static void primary_selection_source_destroy(
		struct wlr_primary_selection_source *wlr_source) {
	struct x11_primary_selection_source *source =
		(struct x11_primary_selection_source *)wlr_source;
	wl_array_release(&source->mime_types_atoms);
	free(source);
}

static const struct wlr_primary_selection_source_impl
		primary_selection_source_impl = {
	.send = primary_selection_source_send,
	.destroy = primary_selection_source_destroy,
};

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
				free(mime_type);
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
			wlr_seat_request_set_selection(xwm->seat, NULL, &source->base,
				wl_display_next_serial(xwm->xwayland->wl_display));
		} else {
			wlr_data_source_destroy(&source->base);
		}
	} else if (selection == &xwm->primary_selection) {
		struct x11_primary_selection_source *source =
			calloc(1, sizeof(struct x11_primary_selection_source));
		if (source == NULL) {
			return;
		}
		wlr_primary_selection_source_init(&source->base,
			&primary_selection_source_impl);

		source->selection = selection;
		wl_array_init(&source->mime_types_atoms);

		bool ok = source_get_targets(selection, &source->base.mime_types,
			&source->mime_types_atoms);
		if (ok) {
			wlr_seat_set_primary_selection(xwm->seat, &source->base,
				wl_display_next_serial(xwm->xwayland->wl_display));
		} else {
			wlr_primary_selection_source_destroy(&source->base);
		}
	} else if (selection == &xwm->dnd_selection) {
		// TODO
	}
}

void xwm_handle_selection_notify(struct wlr_xwm *xwm,
		xcb_selection_notify_event_t *event) {
	wlr_log(WLR_DEBUG, "XCB_SELECTION_NOTIFY (selection=%u, property=%u, target=%u)",
		event->selection, event->property,
		event->target);

	struct wlr_xwm_selection *selection =
		xwm_get_selection(xwm, event->selection);
	if (selection == NULL) {
		return;
	}

	if (event->property == XCB_ATOM_NONE) {
		wlr_log(WLR_ERROR, "convert selection failed");
	} else if (event->target == xwm->atoms[TARGETS]) {
		// No xwayland surface focused, deny access to clipboard
		if (xwm->focus_surface == NULL) {
			wlr_log(WLR_DEBUG, "denying write access to clipboard: "
				"no xwayland surface focused");
			return;
		}

		// This sets the Wayland clipboard (by calling wlr_seat_set_selection)
		xwm_selection_get_targets(selection);
	} else {
		xwm_selection_get_data(selection);
	}
}

int xwm_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_xfixes_selection_notify_event_t *event) {
	wlr_log(WLR_DEBUG, "XCB_XFIXES_SELECTION_NOTIFY (selection=%u, owner=%u)",
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
				wlr_seat_request_set_selection(xwm->seat, NULL, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
			} else if (selection == &xwm->primary_selection) {
				wlr_seat_request_set_primary_selection(xwm->seat, NULL, NULL,
					wl_display_next_serial(xwm->xwayland->wl_display));
			} else if (selection == &xwm->dnd_selection) {
				// TODO: DND
			} else {
				wlr_log(WLR_DEBUG, "X11 selection has been cleared, but cannot "
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
