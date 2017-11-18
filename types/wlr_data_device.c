#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_data_device.h>

#define ALL_ACTIONS (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
		WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
		WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

static uint32_t data_offer_choose_action(struct wlr_data_offer *offer) {
	uint32_t available_actions, preferred_action = 0;
	uint32_t source_actions, offer_actions;

	if (wl_resource_get_version(offer->resource) >=
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		offer_actions = offer->dnd_actions;
		preferred_action = offer->preferred_dnd_action;
	} else {
		offer_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	}

	if (wl_resource_get_version(offer->source->resource) >=
			WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
		source_actions = offer->source->dnd_actions;
	} else {
		source_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	}

	available_actions = offer_actions & source_actions;

	if (!available_actions) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
	}

	if (offer->source->seat_client &&
			offer->source->compositor_action & available_actions) {
		return offer->source->compositor_action;
	}

	// If the dest side has a preferred DnD action, use it
	if ((preferred_action & available_actions) != 0) {
		return preferred_action;
	}

	// Use the first found action, in bit order
	return 1 << (ffs(available_actions) - 1);
}

static void data_offer_update_action(struct wlr_data_offer *offer) {
	uint32_t action;

	if (!offer->source) {
		return;
	}

	action = data_offer_choose_action(offer);

	if (offer->source->current_dnd_action == action) {
		return;
	}

	offer->source->current_dnd_action = action;

	if (offer->in_ask) {
		return;
	}

	if (wl_resource_get_version(offer->source->resource) >=
			WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
		wl_data_source_send_action(offer->source->resource, action);
	}

	if (wl_resource_get_version(offer->resource) >=
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		wl_data_offer_send_action(offer->resource, action);
	}
}

static void client_data_source_accept(struct wlr_data_source *source,
		uint32_t serial, const char *mime_type) {
	wl_data_source_send_target(source->resource, mime_type);
}

static void client_data_source_send(struct wlr_data_source *source,
		const char *mime_type, int32_t fd) {
	wl_data_source_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void client_data_source_cancel(struct wlr_data_source *source) {
	wl_data_source_send_cancelled(source->resource);
}


static void data_offer_accept(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial, const char *mime_type) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (!offer->source || offer != offer->source->offer) {
		return;
	}

	// TODO check that client is currently focused by the input device

	offer->source->accept(offer->source, serial, mime_type);
	offer->source->accepted = (mime_type != NULL);
}

static void data_offer_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int32_t fd) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (offer->source && offer == offer->source->offer) {
		offer->source->send(offer->source, mime_type, fd);
	} else {
		close(fd);
	}
}
static void data_offer_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_notify_finish(struct wlr_data_source *source) {
	if (!source->actions_set) {
		return;
	}

	if (source->offer->in_ask && wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
		wl_data_source_send_action(source->resource,
			source->current_dnd_action);
	}

	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
		wl_data_source_send_dnd_finished(source->resource);
	}

	source->offer = NULL;
}

static void data_offer_finish(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (!offer->source || offer->source->offer != offer) {
		return;
	}

	data_source_notify_finish(offer->source);
}

static void data_offer_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions,
		uint32_t preferred_action) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (dnd_actions & ~ALL_ACTIONS) {
		wl_resource_post_error(offer->resource,
			WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
			"invalid action mask %x", dnd_actions);
		return;
	}

	if (preferred_action && (!(preferred_action & dnd_actions) ||
				__builtin_popcount(preferred_action) > 1)) {
		wl_resource_post_error(offer->resource,
			WL_DATA_OFFER_ERROR_INVALID_ACTION,
			"invalid action %x", preferred_action);
		return;
	}

	offer->dnd_actions = dnd_actions;
	offer->preferred_dnd_action = preferred_action;

	data_offer_update_action(offer);
}

static void data_offer_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (!offer->source) {
		goto out;
	}

	wl_list_remove(&offer->source_destroy.link);

	if (offer->source->offer != offer) {
		goto out;
	}

	// If the drag destination has version < 3, wl_data_offer.finish
	// won't be called, so do this here as a safety net, because
	// we still want the version >=3 drag source to be happy.
	if (wl_resource_get_version(offer->resource) <
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		data_source_notify_finish(offer->source);
	} else if (offer->source->resource &&
			wl_resource_get_version(offer->source->resource) >=
			WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
		wl_data_source_send_cancelled(offer->source->resource);
	}

	offer->source->offer = NULL;
out:
	free(offer);
}

static const struct wl_data_offer_interface data_offer_impl = {
	.accept = data_offer_accept,
	.receive = data_offer_receive,
	.destroy = data_offer_destroy,
	.finish = data_offer_finish,
	.set_actions = data_offer_set_actions,
};

static void handle_offer_source_destroyed(struct wl_listener *listener,
		void *data) {
	struct wlr_data_offer *offer =
		wl_container_of(listener, offer, source_destroy);

	offer->source = NULL;
}

static struct wlr_data_offer *wlr_data_source_send_offer(
		struct wlr_data_source *source,
		struct wl_resource *target) {
	struct wlr_data_offer *offer = calloc(1, sizeof(struct wlr_data_offer));
	if (offer == NULL) {
		return NULL;
	}

	offer->resource =
		wl_resource_create(wl_resource_get_client(target),
			&wl_data_offer_interface,
			wl_resource_get_version(target), 0);
	if (offer->resource == NULL) {
		free(offer);
		return NULL;
	}

	wl_resource_set_implementation(offer->resource, &data_offer_impl, offer,
		data_offer_resource_destroy);

	offer->source_destroy.notify = handle_offer_source_destroyed;
	wl_signal_add(&source->events.destroy, &offer->source_destroy);

	wl_data_device_send_data_offer(target, offer->resource);
	char **p;
	wl_array_for_each(p, &source->mime_types) {
		wl_data_offer_send_offer(offer->resource, *p);
	}

	offer->source = source;
	source->offer = offer;
	source->accepted = false;

	return offer;
}


void wlr_seat_client_send_selection(struct wlr_seat_client *seat_client) {
	if (!seat_client->data_device) {
		return;
	}

	if (seat_client->seat->selection_source) {
		struct wlr_data_offer *offer =
			wlr_data_source_send_offer(seat_client->seat->selection_source,
				seat_client->data_device);
		wl_data_device_send_selection(seat_client->data_device,
			offer->resource);
	} else {
		wl_data_device_send_selection(seat_client->data_device, NULL);
	}
}

static void seat_client_selection_data_source_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, selection_data_source_destroy);

	if (seat->keyboard_state.focused_client &&
			seat->keyboard_state.focused_surface &&
			seat->keyboard_state.focused_client->data_device) {
		wl_data_device_send_selection(
			seat->keyboard_state.focused_client->data_device, NULL);
	}

	seat->selection_source = NULL;

	wl_signal_emit(&seat->events.selection, seat);
}

void wlr_seat_set_selection(struct wlr_seat *seat,
		struct wlr_data_source *source, uint32_t serial) {
	if (seat->selection_source &&
			seat->selection_serial - serial < UINT32_MAX / 2) {
		return;
	}

	if (seat->selection_source) {
		seat->selection_source->cancel(seat->selection_source);
		seat->selection_source = NULL;
		wl_list_remove(&seat->selection_data_source_destroy.link);
	}

	seat->selection_source = source;
	seat->selection_serial = serial;

	struct wlr_seat_client *focused_client =
		seat->keyboard_state.focused_client;

	if (focused_client) {
		wlr_seat_client_send_selection(focused_client);
	}

	wl_signal_emit(&seat->events.selection, seat);

	if (source) {
		seat->selection_data_source_destroy.notify =
			seat_client_selection_data_source_destroy;
		wl_signal_add(&source->events.destroy,
			&seat->selection_data_source_destroy);
	}
}

static void data_device_set_selection(struct wl_client *client,
		struct wl_resource *dd_resource, struct wl_resource *source_resource,
		uint32_t serial) {
	if (!source_resource) {
		return;
	}

	struct wlr_data_source *source = wl_resource_get_user_data(source_resource);
	struct wlr_seat_client *seat_client =
		wl_resource_get_user_data(dd_resource);

	// TODO: store serial and check against incoming serial here
	wlr_seat_set_selection(seat_client->seat, source, serial);
}

static void data_device_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void drag_client_seat_unbound(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag =
		wl_container_of(listener, drag, seat_client_unbound);
	struct wlr_seat_client *unbound_client = data;

	if (drag->focus_client == unbound_client) {
		drag->focus_client = NULL;
		wl_list_remove(&drag->seat_client_unbound.link);
	}
}

static void wlr_drag_set_focus(struct wlr_drag *drag,
		struct wlr_surface *surface, double sx, double sy) {
	if (drag->focus == surface) {
		return;
	}

	if (drag->focus_client && drag->focus_client->data_device) {
		wl_list_remove(&drag->seat_client_unbound.link);
		wl_data_device_send_leave(drag->focus_client->data_device);
		drag->focus_client = NULL;
		drag->focus = NULL;
	}

	if (!surface || !surface->resource) {
		return;
	}

	if (!drag->source &&
			wl_resource_get_client(surface->resource) !=
			wl_resource_get_client(drag->seat_client->wl_resource)) {
		return;
	}

	if (drag->source && drag->source->offer) {
		// unlink the offer from the source
		wl_list_remove(&drag->source->offer->source_destroy.link);
		drag->source->offer->source = NULL;
		drag->source->offer = NULL;
	}

	struct wlr_seat_client *focus_client =
		wlr_seat_client_for_wl_client(drag->seat_client->seat,
			wl_resource_get_client(surface->resource));

	if (!focus_client || !focus_client->data_device) {
		return;
	}

	struct wl_resource *offer_resource = NULL;
	if (drag->source) {
		drag->source->accepted = false;
		struct wlr_data_offer *offer =
			wlr_data_source_send_offer(drag->source, focus_client->data_device);
		if (offer == NULL) {
			return;
		}

		data_offer_update_action(offer);

		if (wl_resource_get_version(offer->resource) >=
				WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
			wl_data_offer_send_source_actions(offer->resource,
				drag->source->dnd_actions);
		}

		offer_resource = offer->resource;
	}

	uint32_t serial =
		wl_display_next_serial(drag->seat_client->seat->display);

	wl_data_device_send_enter(focus_client->data_device, serial,
		surface->resource, wl_fixed_from_double(sx),
		wl_fixed_from_double(sy), offer_resource);

	drag->focus = surface;
	drag->focus_client = focus_client;
	drag->seat_client_unbound.notify = drag_client_seat_unbound;
	wl_signal_add(&focus_client->seat->events.client_unbound,
		&drag->seat_client_unbound);
}

static void wlr_drag_end(struct wlr_drag *drag) {
	if (!drag->cancelling) {
		drag->cancelling = true;
		if (drag->is_pointer_grab) {
			wlr_seat_pointer_end_grab(drag->seat);
		} else {
			wlr_seat_touch_end_grab(drag->seat);
		}
		wlr_seat_keyboard_end_grab(drag->seat);

		if (drag->source) {
			wl_list_remove(&drag->source_destroy.link);
		}

		wlr_drag_set_focus(drag, NULL, 0, 0);

		if (drag->icon) {
			wl_list_remove(&drag->icon_destroy.link);
		}

		free(drag);
	}
}

static void pointer_drag_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_drag *drag = grab->data;
	wlr_drag_set_focus(drag, surface, sx, sy);
}

static void pointer_drag_motion(struct wlr_seat_pointer_grab *grab,
		uint32_t time, double sx, double sy) {
	struct wlr_drag *drag = grab->data;
	if (drag->focus && drag->focus_client && drag->focus_client->data_device) {
		wl_data_device_send_motion(drag->focus_client->data_device, time,
			wl_fixed_from_double(sx), wl_fixed_from_double(sy));
	}
}

static uint32_t pointer_drag_button(struct wlr_seat_pointer_grab *grab,
		uint32_t time, uint32_t button, uint32_t state) {
	struct wlr_drag *drag = grab->data;

	if (drag->source &&
			grab->seat->pointer_state.grab_button == button &&
			state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (drag->focus_client && drag->focus_client->data_device &&
				drag->source->current_dnd_action &&
				drag->source->accepted) {
			wl_data_device_send_drop(drag->focus_client->data_device);
			if (wl_resource_get_version(drag->source->resource) >=
					WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION) {
				wl_data_source_send_dnd_drop_performed(
						drag->source->resource);
			}

			drag->source->offer->in_ask =
				drag->source->current_dnd_action ==
				WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
		} else if (wl_resource_get_version(drag->source->resource) >=
				WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
			wl_data_source_send_cancelled(drag->source->resource);
		}
	}

	if (grab->seat->pointer_state.button_count == 0 &&
			state == WL_POINTER_BUTTON_STATE_RELEASED) {
		wlr_drag_end(drag);
	}

	return 0;
}

static void pointer_drag_axis(struct wlr_seat_pointer_grab *grab, uint32_t time,
		enum wlr_axis_orientation orientation, double value) {
}

static void pointer_drag_cancel(struct wlr_seat_pointer_grab *grab) {
	struct wlr_drag *drag = grab->data;
	wlr_drag_end(drag);
}

const struct
wlr_pointer_grab_interface wlr_data_device_pointer_drag_interface = {
	.enter = pointer_drag_enter,
	.motion = pointer_drag_motion,
	.button = pointer_drag_button,
	.axis = pointer_drag_axis,
	.cancel = pointer_drag_cancel,
};

uint32_t touch_drag_down(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	// eat the event
	return 0;
}

static void touch_drag_up(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	struct wlr_drag *drag = grab->data;
	if (drag->grab_touch_id != point->touch_id) {
		return;
	}

	if (drag->focus_client && drag->focus_client->data_device) {
		wl_data_device_send_drop(drag->focus_client->data_device);
	}

	wlr_drag_end(drag);
}

static void touch_drag_motion(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	struct wlr_drag *drag = grab->data;
	if (drag->focus && drag->focus_client && drag->focus_client->data_device) {
		wl_data_device_send_motion(drag->focus_client->data_device, time,
			wl_fixed_from_double(point->sx), wl_fixed_from_double(point->sy));
	}
}

static void touch_drag_enter(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	struct wlr_drag *drag = grab->data;
	wlr_drag_set_focus(drag, point->focus_surface, point->sx, point->sy);
}

static void touch_drag_cancel(struct wlr_seat_touch_grab *grab) {
	struct wlr_drag *drag = grab->data;
	wlr_drag_end(drag);
}

const struct wlr_touch_grab_interface wlr_data_device_touch_drag_interface = {
	.down = touch_drag_down,
	.up = touch_drag_up,
	.motion = touch_drag_motion,
	.enter = touch_drag_enter,
	.cancel = touch_drag_cancel,
};

static void keyboard_drag_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface) {
	// nothing has keyboard focus during drags
}

static void keyboard_drag_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	// no keyboard input during drags
}

static void keyboard_drag_modifiers(struct wlr_seat_keyboard_grab *grab) {
	//struct wlr_keyboard *keyboard = grab->seat->keyboard_state.keyboard;
	// TODO change the dnd action based on what modifier is pressed on the
	// keyboard
}

static void keyboard_drag_cancel(struct wlr_seat_keyboard_grab *grab) {
	struct wlr_drag *drag = grab->data;
	wlr_drag_end(drag);
}

const struct
wlr_keyboard_grab_interface wlr_data_device_keyboard_drag_interface = {
	.enter = keyboard_drag_enter,
	.key = keyboard_drag_key,
	.modifiers = keyboard_drag_modifiers,
	.cancel = keyboard_drag_cancel,
};

static void drag_handle_icon_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drag *drag = wl_container_of(listener, drag, icon_destroy);
	drag->icon = NULL;
}

static void drag_handle_drag_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_drag *drag = wl_container_of(listener, drag, source_destroy);
	wlr_drag_end(drag);
}

static bool seat_client_start_drag(struct wlr_seat_client *client,
		struct wlr_data_source *source, struct wlr_surface *icon,
		struct wlr_surface *origin, uint32_t serial) {
	struct wlr_drag *drag = calloc(1, sizeof(struct wlr_drag));
	if (drag == NULL) {
		return false;
	}

	drag->seat = client->seat;

	drag->is_pointer_grab = client->pointer != NULL &&
		client->seat->pointer_state.button_count == 1 &&
		client->seat->pointer_state.grab_serial == serial &&
		client->seat->pointer_state.focused_surface &&
		client->seat->pointer_state.focused_surface == origin;

	bool is_touch_grab = client->touch &&
		wlr_seat_touch_num_points(client->seat) == 1 &&
		client->seat->touch_state.grab_serial == serial;

	// set in the iteration
	struct wlr_touch_point *point = NULL;

	if (is_touch_grab) {
		wl_list_for_each(point, &client->seat->touch_state.touch_points, link) {
			is_touch_grab = point->surface && point->surface == origin;
			break;
		}
	}

	if (!drag->is_pointer_grab && !is_touch_grab) {
		free(drag);
		return true;
	}

	if (icon) {
		drag->icon = icon;
		drag->icon_destroy.notify = drag_handle_icon_destroy;
		wl_signal_add(&icon->events.destroy, &drag->icon_destroy);
		drag->icon = icon;
	}

	if (source) {
		drag->source_destroy.notify = drag_handle_drag_source_destroy;
		wl_signal_add(&source->events.destroy, &drag->source_destroy);
		drag->source = source;
	}

	drag->seat_client = client;
	drag->pointer_grab.data = drag;
	drag->pointer_grab.interface = &wlr_data_device_pointer_drag_interface;

	drag->touch_grab.data = drag;
	drag->touch_grab.interface = &wlr_data_device_touch_drag_interface;
	drag->grab_touch_id = drag->seat->touch_state.grab_id;

	drag->keyboard_grab.data = drag;
	drag->keyboard_grab.interface = &wlr_data_device_keyboard_drag_interface;

	wlr_seat_keyboard_start_grab(drag->seat, &drag->keyboard_grab);

	if (drag->is_pointer_grab) {
		wlr_seat_pointer_clear_focus(drag->seat);
		wlr_seat_pointer_start_grab(drag->seat, &drag->pointer_grab);
	} else {
		assert(point);
		wlr_seat_touch_start_grab(drag->seat, &drag->touch_grab);
		wlr_drag_set_focus(drag, point->surface, point->sx, point->sy);
	}

	return true;
}

static void data_device_start_drag(struct wl_client *client,
		struct wl_resource *device_resource,
		struct wl_resource *source_resource,
		struct wl_resource *origin_resource, struct wl_resource *icon_resource,
		uint32_t serial) {
	struct wlr_seat_client *seat_client =
		wl_resource_get_user_data(device_resource);
	struct wlr_surface *origin = wl_resource_get_user_data(origin_resource);
	struct wlr_data_source *source = NULL;
	struct wlr_surface *icon = NULL;

	if (source_resource) {
		source = wl_resource_get_user_data(source_resource);
	}

	if (icon_resource) {
		icon = wl_resource_get_user_data(icon_resource);
	}
	if (icon) {
		if (wlr_surface_set_role(icon, "wl_data_device-icon",
					icon_resource, WL_DATA_DEVICE_ERROR_ROLE) < 0) {
			return;
		}
	}

	// TODO touch grab

	if (!seat_client_start_drag(seat_client, source, icon, origin, serial)) {
		wl_resource_post_no_memory(device_resource);
		return;
	}

	if (source) {
		source->seat_client = seat_client;
	}
}

static const struct wl_data_device_interface data_device_impl = {
	.start_drag = data_device_start_drag,
	.set_selection = data_device_set_selection,
	.release = data_device_release,
};

void data_device_manager_get_data_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_seat_client *seat_client =
		wl_resource_get_user_data(seat_resource);

	struct wl_resource *resource =
		wl_resource_create(client,
			&wl_data_device_interface,
			wl_resource_get_version(manager_resource), id);
	if (resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}

	if (seat_client->data_device != NULL) {
		// XXX this is probably a protocol violation, but it simplfies our code
		// and it's stupid to create several data devices for the same seat.
		wl_resource_destroy(seat_client->data_device);
	}

	seat_client->data_device = resource;

	wl_resource_set_implementation(resource, &data_device_impl,
		seat_client, NULL);
}

static void data_source_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_source *source =
		wl_resource_get_user_data(resource);
	char **p;

	wl_signal_emit(&source->events.destroy, source);

	wl_array_for_each(p, &source->mime_types) {
		free(*p);
	}

	wl_array_release(&source->mime_types);

	free(source);
}

static void data_source_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions) {
	struct wlr_data_source *source =
		wl_resource_get_user_data(resource);

	if (source->actions_set) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"cannot set actions more than once");
		return;
	}

	if (dnd_actions & ~ALL_ACTIONS) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"invalid action mask %x", dnd_actions);
		return;
	}

	if (source->seat_client) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"invalid action change after "
			"wl_data_device.start_drag");
		return;
	}

	source->dnd_actions = dnd_actions;
	source->actions_set = true;
}

static void data_source_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct wlr_data_source *source =
		wl_resource_get_user_data(resource);
	char **p;

	p = wl_array_add(&source->mime_types, sizeof *p);

	if (p) {
		*p = strdup(mime_type);
	}
	if (!p || !*p){
		wl_resource_post_no_memory(resource);
	}
}

static struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void data_device_manager_create_data_source(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_data_source *source = calloc(1, sizeof(struct wlr_data_source));
	if (source == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	source->resource =
		wl_resource_create(client, &wl_data_source_interface,
			wl_resource_get_version(resource), id);
	if (source->resource == NULL) {
		free(source);
		wl_resource_post_no_memory(resource);
		return;
	}

	source->accept = client_data_source_accept;
	source->send = client_data_source_send;
	source->cancel = client_data_source_cancel;

	wl_array_init(&source->mime_types);
	wl_signal_init(&source->events.destroy);

	wl_resource_set_implementation(source->resource, &data_source_impl,
		source, data_source_resource_destroy);
}

static const struct wl_data_device_manager_interface
data_device_manager_impl = {
	.create_data_source = data_device_manager_create_data_source,
	.get_data_device = data_device_manager_get_data_device,
};

static void data_device_manager_bind(struct wl_client *client,
		void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource = wl_resource_create(client,
		&wl_data_device_manager_interface,
		version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &data_device_manager_impl,
		NULL, NULL);
}

struct wlr_data_device_manager *wlr_data_device_manager_create(
		struct wl_display *display) {
	struct wlr_data_device_manager *manager =
		calloc(1, sizeof(struct wlr_data_device_manager));
	if (manager == NULL) {
		wlr_log(L_ERROR, "could not create data device manager");
		return NULL;
	}

	manager->global =
		wl_global_create(display, &wl_data_device_manager_interface,
			3, NULL, data_device_manager_bind);
	if (!manager->global) {
		wlr_log(L_ERROR, "could not create data device manager wl global");
		free(manager);
		return NULL;
	}

	return manager;
}

void wlr_data_device_manager_destroy(struct wlr_data_device_manager *manager) {
	if (!manager) {
		return;
	}
	wl_global_destroy(manager->global);
	free(manager);
}
