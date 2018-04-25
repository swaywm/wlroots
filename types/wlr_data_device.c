#define _XOPEN_SOURCE 700
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "util/signal.h"

#define ALL_ACTIONS (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
		WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
		WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

static const struct wl_data_offer_interface data_offer_impl;

static struct wlr_data_offer *data_offer_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_data_offer_interface,
		&data_offer_impl));
	return wl_resource_get_user_data(resource);
}

static const struct wl_data_source_interface data_source_impl;

static struct client_data_source *client_data_source_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_data_source_interface,
		&data_source_impl));
	return wl_resource_get_user_data(resource);
}

static uint32_t data_offer_choose_action(struct wlr_data_offer *offer) {
	uint32_t offer_actions, preferred_action = 0;
	if (wl_resource_get_version(offer->resource) >=
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		offer_actions = offer->actions;
		preferred_action = offer->preferred_action;
	} else {
		offer_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	}

	uint32_t source_actions;
	if (offer->source->actions >= 0) {
		source_actions = offer->source->actions;
	} else {
		source_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	}

	uint32_t available_actions = offer_actions & source_actions;
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
	if (!offer->source) {
		return;
	}

	uint32_t action = data_offer_choose_action(offer);
	if (offer->source->current_dnd_action == action) {
		return;
	}

	offer->source->current_dnd_action = action;

	if (offer->in_ask) {
		return;
	}

	wlr_data_source_dnd_action(offer->source, action);

	if (wl_resource_get_version(offer->resource) >=
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		wl_data_offer_send_action(offer->resource, action);
	}
}

static void data_offer_accept(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial, const char *mime_type) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (!offer->source || offer != offer->source->offer) {
		return;
	}

	// TODO check that client is currently focused by the input device

	wlr_data_source_accept(offer->source, serial, mime_type);
}

static void data_offer_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int32_t fd) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (offer->source && offer == offer->source->offer) {
		wlr_data_source_send(offer->source, mime_type, fd);
	} else {
		close(fd);
	}
}
static void data_offer_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_notify_finish(struct wlr_data_source *source) {
	assert(source->offer);
	if (source->actions < 0) {
		return;
	}

	if (source->offer->in_ask) {
		wlr_data_source_dnd_action(source, source->current_dnd_action);
	}

	source->offer = NULL;
	wlr_data_source_dnd_finish(source);
}

static void data_offer_finish(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (!offer->source || offer->source->offer != offer) {
		return;
	}

	data_source_notify_finish(offer->source);
}

static void data_offer_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t actions,
		uint32_t preferred_action) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (actions & ~ALL_ACTIONS) {
		wl_resource_post_error(offer->resource,
			WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
			"invalid action mask %x", actions);
		return;
	}

	if (preferred_action && (!(preferred_action & actions) ||
			__builtin_popcount(preferred_action) > 1)) {
		wl_resource_post_error(offer->resource,
			WL_DATA_OFFER_ERROR_INVALID_ACTION,
			"invalid action %x", preferred_action);
		return;
	}

	offer->actions = actions;
	offer->preferred_action = preferred_action;

	data_offer_update_action(offer);
}

static void data_offer_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (!offer->source) {
		goto out;
	}

	wl_list_remove(&offer->source_destroy.link);

	if (offer->source->offer != offer) {
		goto out;
	}

	// If the drag destination has version < 3, wl_data_offer.finish
	// won't be called, so do this here as a safety net, because
	// we still want the version >= 3 drag source to be happy.
	if (wl_resource_get_version(offer->resource) <
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		data_source_notify_finish(offer->source);
		offer->source->offer = NULL;
	} else if (offer->source->impl->dnd_finish) {
		// source->cancel can free the source
		offer->source->offer = NULL;
		wlr_data_source_cancel(offer->source);
	} else {
		offer->source->offer = NULL;
	}

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

static struct wlr_data_offer *data_source_send_offer(
		struct wlr_data_source *source,
		struct wlr_seat_client *target) {
	if (wl_list_empty(&target->data_devices)) {
		return NULL;
	}

	struct wlr_data_offer *offer = calloc(1, sizeof(struct wlr_data_offer));
	if (offer == NULL) {
		return NULL;
	}

	uint32_t version = wl_resource_get_version(
		wl_resource_from_link(target->data_devices.next));
	offer->resource = wl_resource_create(target->client,
		&wl_data_offer_interface, version, 0);
	if (offer->resource == NULL) {
		free(offer);
		return NULL;
	}
	wl_resource_set_implementation(offer->resource, &data_offer_impl, offer,
		data_offer_resource_destroy);

	offer->source_destroy.notify = handle_offer_source_destroyed;
	wl_signal_add(&source->events.destroy, &offer->source_destroy);

	struct wl_resource *target_resource;
	wl_resource_for_each(target_resource, &target->data_devices) {
		wl_data_device_send_data_offer(target_resource, offer->resource);
	}

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
	if (wl_list_empty(&seat_client->data_devices)) {
		return;
	}

	if (seat_client->seat->selection_source) {
		struct wlr_data_offer *offer = data_source_send_offer(
			seat_client->seat->selection_source, seat_client);
		if (offer == NULL) {
			return;
		}

		struct wl_resource *resource;
		wl_resource_for_each(resource, &seat_client->data_devices) {
			wl_data_device_send_selection(resource, offer->resource);
		}
	} else {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &seat_client->data_devices) {
			wl_data_device_send_selection(resource, NULL);
		}
	}
}

static void seat_client_selection_source_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, selection_source_destroy);
	struct wlr_seat_client *seat_client = seat->keyboard_state.focused_client;

	if (seat_client && seat->keyboard_state.focused_surface) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &seat_client->data_devices) {
			wl_data_device_send_selection(resource, NULL);
		}
	}

	seat->selection_source = NULL;

	wlr_signal_emit_safe(&seat->events.selection, seat);
}

void wlr_seat_set_selection(struct wlr_seat *seat,
		struct wlr_data_source *source, uint32_t serial) {
	if (seat->selection_source &&
			seat->selection_serial - serial < UINT32_MAX / 2) {
		return;
	}

	if (seat->selection_source) {
		wl_list_remove(&seat->selection_source_destroy.link);
		wlr_data_source_cancel(seat->selection_source);
		seat->selection_source = NULL;
	}

	seat->selection_source = source;
	seat->selection_serial = serial;

	struct wlr_seat_client *focused_client =
		seat->keyboard_state.focused_client;

	if (focused_client) {
		wlr_seat_client_send_selection(focused_client);
	}

	wlr_signal_emit_safe(&seat->events.selection, seat);

	if (source) {
		seat->selection_source_destroy.notify =
			seat_client_selection_source_destroy;
		wl_signal_add(&source->events.destroy,
			&seat->selection_source_destroy);
	}
}

static const struct wl_data_device_interface data_device_impl;

static struct wlr_seat_client *seat_client_from_data_device_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_data_device_interface,
		&data_device_impl));
	return wl_resource_get_user_data(resource);
}

static void data_device_set_selection(struct wl_client *client,
		struct wl_resource *device_resource,
		struct wl_resource *source_resource, uint32_t serial) {
	struct client_data_source *source = NULL;
	if (source_resource != NULL) {
		source = client_data_source_from_resource(source_resource);
	}

	struct wlr_seat_client *seat_client =
		seat_client_from_data_device_resource(device_resource);

	struct wlr_data_source *wlr_source = (struct wlr_data_source *)source;
	wlr_seat_set_selection(seat_client->seat, wlr_source, serial);
}

static void data_device_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void handle_drag_seat_client_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_drag *drag =
		wl_container_of(listener, drag, seat_client_destroy);

	drag->focus_client = NULL;
	wl_list_remove(&drag->seat_client_destroy.link);
}

static void drag_set_focus(struct wlr_drag *drag,
		struct wlr_surface *surface, double sx, double sy) {
	if (drag->focus == surface) {
		return;
	}

	if (drag->focus_client) {
		wl_list_remove(&drag->seat_client_destroy.link);

		struct wl_resource *resource;
		wl_resource_for_each(resource, &drag->focus_client->data_devices) {
			wl_data_device_send_leave(resource);
		}

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
	if (!focus_client) {
		return;
	}

	struct wl_resource *offer_resource = NULL;
	if (drag->source) {
		drag->source->accepted = false;
		struct wlr_data_offer *offer = data_source_send_offer(drag->source,
			focus_client);
		if (offer != NULL) {
			data_offer_update_action(offer);

			if (wl_resource_get_version(offer->resource) >=
					WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
				wl_data_offer_send_source_actions(offer->resource,
					drag->source->actions);
			}

			offer_resource = offer->resource;
		}
	}

	if (!wl_list_empty(&focus_client->data_devices)) {
		uint32_t serial =
			wl_display_next_serial(drag->seat_client->seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &focus_client->data_devices) {
			wl_data_device_send_enter(resource, serial, surface->resource,
				wl_fixed_from_double(sx), wl_fixed_from_double(sy),
				offer_resource);
		}
	}

	drag->focus = surface;
	drag->focus_client = focus_client;
	drag->seat_client_destroy.notify = handle_drag_seat_client_destroy;
	wl_signal_add(&focus_client->events.destroy, &drag->seat_client_destroy);

	wlr_signal_emit_safe(&drag->events.focus, drag);
}

static void drag_end(struct wlr_drag *drag) {
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

		drag_set_focus(drag, NULL, 0, 0);

		if (drag->icon) {
			drag->icon->mapped = false;
			wl_list_remove(&drag->icon_destroy.link);
			wlr_signal_emit_safe(&drag->icon->events.map, drag->icon);
		}

		wlr_signal_emit_safe(&drag->events.destroy, drag);
		free(drag);
	}
}

static void pointer_drag_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_drag *drag = grab->data;
	drag_set_focus(drag, surface, sx, sy);
}

static void pointer_drag_motion(struct wlr_seat_pointer_grab *grab,
		uint32_t time, double sx, double sy) {
	struct wlr_drag *drag = grab->data;
	if (drag->focus != NULL && drag->focus_client != NULL) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &drag->focus_client->data_devices) {
			wl_data_device_send_motion(resource, time, wl_fixed_from_double(sx),
				wl_fixed_from_double(sy));
		}

		struct wlr_drag_motion_event event = {
			.drag = drag,
			.time = time,
			.sx = sx,
			.sy = sy,
		};
		wlr_signal_emit_safe(&drag->events.motion, &event);
	}
}

static uint32_t pointer_drag_button(struct wlr_seat_pointer_grab *grab,
		uint32_t time, uint32_t button, uint32_t state) {
	struct wlr_drag *drag = grab->data;

	if (drag->source &&
			grab->seat->pointer_state.grab_button == button &&
			state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (drag->focus_client && drag->source->current_dnd_action &&
				drag->source->accepted) {
			struct wl_resource *resource;
			wl_resource_for_each(resource, &drag->focus_client->data_devices) {
				wl_data_device_send_drop(resource);
			}
			wlr_data_source_dnd_drop(drag->source);

			if (drag->source->offer != NULL) {
				drag->source->offer->in_ask =
					drag->source->current_dnd_action ==
					WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
			}

			struct wlr_drag_drop_event event = {
				.drag = drag,
				.time = time,
			};
			wlr_signal_emit_safe(&drag->events.drop, &event);
		} else if (drag->source->impl->dnd_finish) {
			wlr_data_source_cancel(drag->source);
		}
	}

	if (grab->seat->pointer_state.button_count == 0 &&
			state == WL_POINTER_BUTTON_STATE_RELEASED) {
		drag_end(drag);
	}

	return 0;
}

static void pointer_drag_axis(struct wlr_seat_pointer_grab *grab, uint32_t time,
		enum wlr_axis_orientation orientation, double value) {
	// This space is intentionally left blank
}

static void pointer_drag_cancel(struct wlr_seat_pointer_grab *grab) {
	struct wlr_drag *drag = grab->data;
	drag_end(drag);
}

static const struct wlr_pointer_grab_interface
		data_device_pointer_drag_interface = {
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

	if (drag->focus_client) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &drag->focus_client->data_devices) {
			wl_data_device_send_drop(resource);
		}
	}

	drag_end(drag);
}

static void touch_drag_motion(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	struct wlr_drag *drag = grab->data;
	if (drag->focus && drag->focus_client) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &drag->focus_client->data_devices) {
			wl_data_device_send_motion(resource, time,
				wl_fixed_from_double(point->sx),
				wl_fixed_from_double(point->sy));
		}
	}
}

static void touch_drag_enter(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	struct wlr_drag *drag = grab->data;
	drag_set_focus(drag, point->focus_surface, point->sx, point->sy);
}

static void touch_drag_cancel(struct wlr_seat_touch_grab *grab) {
	struct wlr_drag *drag = grab->data;
	drag_end(drag);
}

static const struct wlr_touch_grab_interface
		data_device_touch_drag_interface = {
	.down = touch_drag_down,
	.up = touch_drag_up,
	.motion = touch_drag_motion,
	.enter = touch_drag_enter,
	.cancel = touch_drag_cancel,
};

static void keyboard_drag_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	// nothing has keyboard focus during drags
}

static void keyboard_drag_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	// no keyboard input during drags
}

static void keyboard_drag_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	//struct wlr_keyboard *keyboard = grab->seat->keyboard_state.keyboard;
	// TODO change the dnd action based on what modifier is pressed on the
	// keyboard
}

static void keyboard_drag_cancel(struct wlr_seat_keyboard_grab *grab) {
	struct wlr_drag *drag = grab->data;
	drag_end(drag);
}

static const struct wlr_keyboard_grab_interface
		data_device_keyboard_drag_interface = {
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
	drag_end(drag);
}

static void drag_icon_destroy(struct wlr_drag_icon *icon) {
	if (!icon) {
		return;
	}
	wlr_signal_emit_safe(&icon->events.destroy, icon);
	wlr_surface_set_role_committed(icon->surface, NULL, NULL);
	wl_list_remove(&icon->surface_destroy.link);
	wl_list_remove(&icon->seat_client_destroy.link);
	wl_list_remove(&icon->link);
	free(icon);
}

static void handle_drag_icon_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_drag_icon *icon =
		wl_container_of(listener, icon, surface_destroy);
	drag_icon_destroy(icon);
}

static void handle_drag_icon_surface_commit(struct wlr_surface *surface,
		void *role_data) {
	struct wlr_drag_icon *icon = role_data;
	icon->sx += icon->surface->current->sx;
	icon->sy += icon->surface->current->sy;
}

static void handle_drag_icon_seat_client_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_drag_icon *icon =
		wl_container_of(listener, icon, seat_client_destroy);

	drag_icon_destroy(icon);
}

static struct wlr_drag_icon *drag_icon_create(
		struct wlr_surface *icon_surface, struct wlr_seat_client *client,
		bool is_pointer, int32_t touch_id) {
	struct wlr_drag_icon *icon = calloc(1, sizeof(struct wlr_drag_icon));
	if (!icon) {
		return NULL;
	}

	icon->surface = icon_surface;
	icon->client = client;
	icon->is_pointer = is_pointer;
	icon->touch_id = touch_id;
	icon->mapped = true;

	wl_signal_init(&icon->events.map);
	wl_signal_init(&icon->events.destroy);

	wl_signal_add(&icon->surface->events.destroy, &icon->surface_destroy);
	icon->surface_destroy.notify = handle_drag_icon_surface_destroy;

	wlr_surface_set_role_committed(icon->surface,
		handle_drag_icon_surface_commit, icon);

	wl_signal_add(&client->events.destroy, &icon->seat_client_destroy);
	icon->seat_client_destroy.notify = handle_drag_icon_seat_client_destroy;

	wl_list_insert(&client->seat->drag_icons, &icon->link);
	wlr_signal_emit_safe(&client->seat->events.new_drag_icon, icon);

	return icon;
}

static void seat_handle_drag_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, drag_source_destroy);
	wl_list_remove(&seat->drag_source_destroy.link);
	seat->drag_source = NULL;
}

static bool seat_client_start_drag(struct wlr_seat_client *client,
		struct wlr_data_source *source, struct wlr_surface *icon_surface,
		struct wlr_surface *origin, uint32_t serial) {
	struct wlr_drag *drag = calloc(1, sizeof(struct wlr_drag));
	if (drag == NULL) {
		return false;
	}

	wl_signal_init(&drag->events.focus);
	wl_signal_init(&drag->events.motion);
	wl_signal_init(&drag->events.drop);
	wl_signal_init(&drag->events.destroy);

	struct wlr_seat *seat = client->seat;
	drag->seat = seat;

	drag->is_pointer_grab = !wl_list_empty(&client->pointers) &&
		seat->pointer_state.button_count == 1 &&
		seat->pointer_state.grab_serial == serial &&
		seat->pointer_state.focused_surface &&
		seat->pointer_state.focused_surface == origin;

	bool is_touch_grab = !wl_list_empty(&client->touches) &&
		wlr_seat_touch_num_points(seat) == 1 &&
		seat->touch_state.grab_serial == serial;

	// set in the iteration
	struct wlr_touch_point *point = NULL;
	if (is_touch_grab) {
		wl_list_for_each(point, &seat->touch_state.touch_points, link) {
			is_touch_grab = point->surface && point->surface == origin;
			break;
		}
	}

	if (!drag->is_pointer_grab && !is_touch_grab) {
		free(drag);
		return true;
	}

	if (icon_surface) {
		int32_t touch_id = (point ? point->touch_id : 0);
		struct wlr_drag_icon *icon =
			drag_icon_create(icon_surface, client, drag->is_pointer_grab,
				touch_id);
		if (!icon) {
			free(drag);
			return false;
		}

		drag->icon = icon;
		drag->icon_destroy.notify = drag_handle_icon_destroy;
		wl_signal_add(&icon->events.destroy, &drag->icon_destroy);
	}

	drag->source = source;
	if (source != NULL) {
		drag->source_destroy.notify = drag_handle_drag_source_destroy;
		wl_signal_add(&source->events.destroy, &drag->source_destroy);
	}

	drag->seat_client = client;
	drag->pointer_grab.data = drag;
	drag->pointer_grab.interface = &data_device_pointer_drag_interface;

	drag->touch_grab.data = drag;
	drag->touch_grab.interface = &data_device_touch_drag_interface;
	drag->grab_touch_id = seat->touch_state.grab_id;

	drag->keyboard_grab.data = drag;
	drag->keyboard_grab.interface = &data_device_keyboard_drag_interface;

	wlr_seat_keyboard_start_grab(seat, &drag->keyboard_grab);

	if (drag->is_pointer_grab) {
		wlr_seat_pointer_clear_focus(seat);
		wlr_seat_pointer_start_grab(seat, &drag->pointer_grab);
	} else {
		assert(point);
		wlr_seat_touch_start_grab(seat, &drag->touch_grab);
		drag_set_focus(drag, point->surface, point->sx, point->sy);
	}

	seat->drag = drag; // TODO: unset this thing somewhere
	seat->drag_serial = serial;

	seat->drag_source = source;
	if (source != NULL) {
		seat->drag_source_destroy.notify = seat_handle_drag_source_destroy;
		wl_signal_add(&source->events.destroy, &seat->drag_source_destroy);
	}

	wlr_signal_emit_safe(&seat->events.start_drag, drag);

	return true;
}

static void data_device_start_drag(struct wl_client *client,
		struct wl_resource *device_resource,
		struct wl_resource *source_resource,
		struct wl_resource *origin_resource, struct wl_resource *icon_resource,
		uint32_t serial) {
	struct wlr_seat_client *seat_client =
		seat_client_from_data_device_resource(device_resource);
	struct wlr_surface *origin = wlr_surface_from_resource(origin_resource);
	struct wlr_data_source *source = NULL;
	struct wlr_surface *icon = NULL;

	if (source_resource) {
		struct client_data_source *client_source =
			client_data_source_from_resource(source_resource);
		source = (struct wlr_data_source *)client_source;
	}

	if (icon_resource) {
		icon = wlr_surface_from_resource(icon_resource);
	}
	if (icon) {
		if (wlr_surface_set_role(icon, "wl_data_device-icon",
					icon_resource, WL_DATA_DEVICE_ERROR_ROLE) < 0) {
			return;
		}
	}

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

static void data_device_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}


struct client_data_source {
	struct wlr_data_source source;
	struct wlr_data_source_impl impl;
	struct wl_resource *resource;
};

static void client_data_source_accept(struct wlr_data_source *wlr_source,
	uint32_t serial, const char *mime_type);

static struct client_data_source *client_data_source_from_wlr_data_source(
		struct wlr_data_source *wlr_source) {
	assert(wlr_source->impl->accept == client_data_source_accept);
	return (struct client_data_source *)wlr_source;
}

static void client_data_source_accept(struct wlr_data_source *wlr_source,
		uint32_t serial, const char *mime_type) {
	struct client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	wl_data_source_send_target(source->resource, mime_type);
}

static void client_data_source_send(struct wlr_data_source *wlr_source,
		const char *mime_type, int32_t fd) {
	struct client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	wl_data_source_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void client_data_source_cancel(struct wlr_data_source *wlr_source) {
	struct client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	wl_data_source_send_cancelled(source->resource);
}

static void client_data_source_dnd_drop(struct wlr_data_source *wlr_source) {
	struct client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	assert(wl_resource_get_version(source->resource) >=
		WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION);
	wl_data_source_send_dnd_drop_performed(source->resource);
}

static void client_data_source_dnd_finish(struct wlr_data_source *wlr_source) {
	struct client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	assert(wl_resource_get_version(source->resource) >=
		WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION);
	wl_data_source_send_dnd_finished(source->resource);
}

static void client_data_source_dnd_action(struct wlr_data_source *wlr_source,
		enum wl_data_device_manager_dnd_action action) {
	struct client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	assert(wl_resource_get_version(source->resource) >=
		WL_DATA_SOURCE_ACTION_SINCE_VERSION);
	wl_data_source_send_action(source->resource, action);
}

static void data_source_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct client_data_source *client_data_source_create(
		struct wl_resource *source_resource) {
	struct client_data_source *source =
		calloc(1, sizeof(struct client_data_source));
	if (source == NULL) {
		return NULL;
	}

	source->resource = source_resource;

	source->impl.accept = client_data_source_accept;
	source->impl.send = client_data_source_send;
	source->impl.cancel = client_data_source_cancel;

	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION) {
		source->impl.dnd_drop = client_data_source_dnd_drop;
	}
	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
		source->impl.dnd_finish = client_data_source_dnd_finish;
	}
	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
		source->impl.dnd_action = client_data_source_dnd_action;
	}

	wlr_data_source_init(&source->source, &source->impl);
	return source;
}

static void data_source_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions) {
	struct client_data_source *source =
		client_data_source_from_resource(resource);

	if (source->source.actions >= 0) {
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

	if (source->source.seat_client) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"invalid action change after "
			"wl_data_device.start_drag");
		return;
	}

	source->source.actions = dnd_actions;
}

static void data_source_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct client_data_source *source =
		client_data_source_from_resource(resource);

	char **p = wl_array_add(&source->source.mime_types, sizeof(*p));
	if (p) {
		*p = strdup(mime_type);
	}
	if (!p || !*p) {
		if (p) {
			source->source.mime_types.size -= sizeof(*p);
		}
		wl_resource_post_no_memory(resource);
	}
}

static const struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void data_source_resource_handle_destroy(struct wl_resource *resource) {
	struct client_data_source *source =
		client_data_source_from_resource(resource);
	wlr_data_source_finish(&source->source);
	free(source);
}

void wlr_data_source_init(struct wlr_data_source *source,
		const struct wlr_data_source_impl *impl) {
	assert(impl->send);

	source->impl = impl;
	wl_array_init(&source->mime_types);
	wl_signal_init(&source->events.destroy);
	source->actions = -1;
}

void wlr_data_source_finish(struct wlr_data_source *source) {
	if (source == NULL) {
		return;
	}

	wlr_signal_emit_safe(&source->events.destroy, source);

	char **p;
	wl_array_for_each(p, &source->mime_types) {
		free(*p);
	}
	wl_array_release(&source->mime_types);
}

void wlr_data_source_send(struct wlr_data_source *source, const char *mime_type,
		int32_t fd) {
	source->impl->send(source, mime_type, fd);
}

void wlr_data_source_accept(struct wlr_data_source *source, uint32_t serial,
		const char *mime_type) {
	source->accepted = (mime_type != NULL);
	if (source->impl->accept) {
		source->impl->accept(source, serial, mime_type);
	}
}

void wlr_data_source_cancel(struct wlr_data_source *source) {
	if (source->impl->cancel) {
		source->impl->cancel(source);
	}
}

void wlr_data_source_dnd_drop(struct wlr_data_source *source) {
	if (source->impl->dnd_drop) {
		source->impl->dnd_drop(source);
	}
}

void wlr_data_source_dnd_finish(struct wlr_data_source *source) {
	if (source->impl->dnd_finish) {
		source->impl->dnd_finish(source);
	}
}

void wlr_data_source_dnd_action(struct wlr_data_source *source,
		enum wl_data_device_manager_dnd_action action) {
	source->current_dnd_action = action;
	if (source->impl->dnd_action) {
		source->impl->dnd_action(source, action);
	}
}


void data_device_manager_get_data_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);

	struct wl_resource *resource = wl_resource_create(client,
		&wl_data_device_interface, wl_resource_get_version(manager_resource),
		id);
	if (resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, &data_device_impl, seat_client,
		&data_device_destroy);
	wl_list_insert(&seat_client->data_devices, wl_resource_get_link(resource));
}

static void data_device_manager_create_data_source(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wl_resource *source_resource = wl_resource_create(client,
		&wl_data_source_interface, wl_resource_get_version(resource), id);
	if (source_resource == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	struct client_data_source *source =
		client_data_source_create(source_resource);
	if (source == NULL) {
		wl_resource_destroy(source_resource);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(source_resource, &data_source_impl,
		source, data_source_resource_handle_destroy);
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

void wlr_data_device_manager_destroy(struct wlr_data_device_manager *manager) {
	if (!manager) {
		return;
	}
	wl_list_remove(&manager->display_destroy.link);
	// TODO: free wl_resources
	wl_global_destroy(manager->global);
	free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_data_device_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_data_device_manager_destroy(manager);
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

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
