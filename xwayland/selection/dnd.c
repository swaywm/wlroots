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
#include "xwayland/selection.h"

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

/**
 * Handle a status message for an outgoing DnD operation.
 */
static void xwm_handle_dnd_status(struct wlr_xwm *xwm,
		xcb_client_message_data_t *data) {
	if (xwm->drag == NULL) {
		wlr_log(L_DEBUG, "ignoring XdndStatus client message because "
			"there's no drag");
		return;
	}

	xcb_window_t target_window = data->data32[0];
	bool accepted = data->data32[1] & 1;
	xcb_atom_t action_atom = data->data32[4];

	if (xwm->drag_focus == NULL ||
			target_window != xwm->drag_focus->window_id) {
		wlr_log(L_DEBUG, "ignoring XdndStatus client message because "
			"it doesn't match the current drag focus window ID");
		return;
	}

	enum wl_data_device_manager_dnd_action action =
		data_device_manager_dnd_action_from_atom(xwm, action_atom);

	struct wlr_drag *drag = xwm->drag;
	assert(drag != NULL);

	drag->source->accepted = accepted;
	wlr_data_source_dnd_action(drag->source, action);

	wlr_log(L_DEBUG, "DND_STATUS window=%d accepted=%d action=%d",
		target_window, accepted, action);
}

/**
 * Handle a finish message for an outgoing DnD operation.
 */
static void xwm_handle_dnd_finished(struct wlr_xwm *xwm,
		xcb_client_message_data_t *data) {
	// This should only happen after the drag has ended, but before the drag
	// source is destroyed
	if (xwm->seat == NULL || xwm->seat->drag_source == NULL ||
			xwm->drag != NULL) {
		wlr_log(L_DEBUG, "ignoring XdndFinished client message because "
			"there's no finished drag");
		return;
	}

	struct wlr_data_source *source = xwm->seat->drag_source;

	xcb_window_t target_window = data->data32[0];
	bool performed = data->data32[1] & 1;
	xcb_atom_t action_atom = data->data32[2];

	if (xwm->drag_focus == NULL ||
			target_window != xwm->drag_focus->window_id) {
		wlr_log(L_DEBUG, "ignoring XdndFinished client message because "
			"it doesn't match the finished drag focus window ID");
		return;
	}

	enum wl_data_device_manager_dnd_action action =
		data_device_manager_dnd_action_from_atom(xwm, action_atom);

	if (performed) {
		wlr_data_source_dnd_finish(source);
	}

	wlr_log(L_DEBUG, "DND_FINISH window=%d performed=%d action=%d",
		target_window, performed, action);
}

static bool xwm_add_atom_to_mime_types(struct wlr_xwm *xwm,
		struct wl_array *mime_types, xcb_atom_t atom) {
	char *mime_type = xwm_mime_type_from_atom(xwm, atom);
	if (mime_type == NULL) {
		return false;
	}
	char **mime_type_ptr =
		wl_array_add(mime_types, sizeof(*mime_type_ptr));
	if (mime_type_ptr == NULL) {
		return false;
	}
	*mime_type_ptr = mime_type;
	return true;
}

static bool xwm_dnd_get_mime_types(struct wlr_xwm *xwm,
		struct wl_array *mime_types, xcb_window_t source) {
	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn,
		1, // delete
		source,
		xwm->atoms[DND_TYPE_LIST],
		XCB_GET_PROPERTY_TYPE_ANY,
		0, // offset
		0x1fffffff // length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL) {
		return false;
	}
	if (reply->type != XCB_ATOM_ATOM || reply->value_len == 0) {
		wlr_log(L_ERROR, "invalid XdndTypeList property");
		goto error;
	}

	xcb_atom_t *atoms = xcb_get_property_value(reply);
	for (uint32_t i = 0; i < reply->value_len; ++i) {
		if (!xwm_add_atom_to_mime_types(xwm, mime_types, atoms[i])) {
			wlr_log(L_ERROR, "failed to add MIME type atom to list");
			goto error;
		}
	}

	free(reply);
	return true;

error:
	free(reply);
	return false;
}

/**
 * Handle an enter message for an incoming DnD operation.
 */
static void xwm_handle_dnd_enter(struct wlr_xwm *xwm,
		xcb_client_message_data_t *data) {
	if (xwm->seat == NULL) {
		wlr_log(L_DEBUG, "ignoring XdndEnter client message because "
			"there's no Xwayland seat");
		return;
	}

	xcb_window_t source_window = data->data32[0];

	if (source_window != xwm->dnd_selection.owner) {
		wlr_log(L_DEBUG, "ignoring XdndEnter client message because "
			"the source window hasn't set the drag-and-drop selection");
		return;
	}

	struct wl_array mime_types;
	wl_array_init(&mime_types);
	if ((data->data32[1] & 1) == 0) {
		// Less than 3 MIME types, those are in the message data
		for (size_t i = 0; i < 3; ++i) {
			xcb_atom_t atom = data->data32[2+i];
			if (atom == XCB_ATOM_NONE) {
				break;
			}
			if (!xwm_add_atom_to_mime_types(xwm, &mime_types, atom)) {
				wlr_log(L_ERROR, "failed to add MIME type atom to list");
				break;
			}
		}
	} else {
		if (!xwm_dnd_get_mime_types(xwm, &mime_types, source_window)) {
			wlr_log(L_ERROR, "failed to add MIME type atom to list");
		}
	}

	wlr_log(L_INFO, "parsed XdndEnter");
}

int xwm_handle_selection_client_message(struct wlr_xwm *xwm,
		xcb_client_message_event_t *ev) {
	// Outgoing
	if (ev->type == xwm->atoms[DND_STATUS]) {
		xwm_handle_dnd_status(xwm, &ev->data);
		return 1;
	} else if (ev->type == xwm->atoms[DND_FINISHED]) {
		xwm_handle_dnd_finished(xwm, &ev->data);
		return 1;
	}

	// Incoming
	if (ev->type == xwm->atoms[DND_ENTER]) {
		xwm_handle_dnd_enter(xwm, &ev->data);
		return 1;
	}

	return 0;
}

int xwm_dnd_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_xfixes_selection_notify_event_t *event) {
	assert(event->selection == xwm->atoms[DND_SELECTION]);
	struct wlr_xwm_selection *selection = &xwm->dnd_selection;

	selection->owner = event->owner;

	if (event->owner != XCB_ATOM_NONE) {
		// TODO: start grab
	} else {
		// TODO: end grab
	}

	return 1;
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

void xwm_seat_handle_start_drag(struct wlr_xwm *xwm, struct wlr_drag *drag) {
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
