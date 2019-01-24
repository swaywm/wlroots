#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "types/wlr_data_device.h"
#include "util/signal.h"

static void drag_handle_seat_client_destroy(struct wl_listener *listener,
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
			drag->seat_client->client) {
		return;
	}

	struct wlr_seat_client *focus_client = wlr_seat_client_for_wl_client(
		drag->seat_client->seat, wl_resource_get_client(surface->resource));
	if (!focus_client) {
		return;
	}

	if (drag->source != NULL) {
		drag->source->accepted = false;

		uint32_t serial =
			wl_display_next_serial(drag->seat_client->seat->display);

		struct wl_resource *device_resource;
		wl_resource_for_each(device_resource, &focus_client->data_devices) {
			struct wlr_data_offer *offer = data_offer_create(device_resource,
				drag->source, WLR_DATA_OFFER_DRAG);
			if (offer == NULL) {
				wl_resource_post_no_memory(device_resource);
				return;
			}

			data_offer_update_action(offer);

			if (wl_resource_get_version(offer->resource) >=
					WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
				wl_data_offer_send_source_actions(offer->resource,
					drag->source->actions);
			}

			wl_data_device_send_enter(device_resource, serial,
				surface->resource,
				wl_fixed_from_double(sx), wl_fixed_from_double(sy),
				offer->resource);
		}
	}

	drag->focus = surface;
	drag->focus_client = focus_client;
	drag->seat_client_destroy.notify = drag_handle_seat_client_destroy;
	wl_signal_add(&focus_client->events.destroy, &drag->seat_client_destroy);

	wlr_signal_emit_safe(&drag->events.focus, drag);
}

static void drag_icon_set_mapped(struct wlr_drag_icon *icon, bool mapped) {
	if (mapped && !icon->mapped) {
		icon->mapped = true;
		wlr_signal_emit_safe(&icon->events.map, icon);
	} else if (!mapped && icon->mapped) {
		icon->mapped = false;
		wlr_signal_emit_safe(&icon->events.unmap, icon);
	}
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
			wl_list_remove(&drag->icon_destroy.link);
			drag_icon_set_mapped(drag->icon, false);
		}

		wlr_signal_emit_safe(&drag->events.destroy, drag);
		free(drag);
	}
}

static void drag_handle_pointer_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_drag *drag = grab->data;
	drag_set_focus(drag, surface, sx, sy);
}

static void drag_handle_pointer_motion(struct wlr_seat_pointer_grab *grab,
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

static uint32_t drag_handle_pointer_button(struct wlr_seat_pointer_grab *grab,
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

			struct wlr_drag_drop_event event = {
				.drag = drag,
				.time = time,
			};
			wlr_signal_emit_safe(&drag->events.drop, &event);
		} else if (drag->source->impl->dnd_finish) {
			wlr_data_source_destroy(drag->source);
		}
	}

	if (grab->seat->pointer_state.button_count == 0 &&
			state == WL_POINTER_BUTTON_STATE_RELEASED) {
		drag_end(drag);
	}

	return 0;
}

static void drag_handle_pointer_axis(struct wlr_seat_pointer_grab *grab,
		uint32_t time, enum wlr_axis_orientation orientation, double value,
		int32_t value_discrete, enum wlr_axis_source source) {
	// This space is intentionally left blank
}

static void drag_handle_pointer_cancel(struct wlr_seat_pointer_grab *grab) {
	struct wlr_drag *drag = grab->data;
	drag_end(drag);
}

static const struct wlr_pointer_grab_interface
		data_device_pointer_drag_interface = {
	.enter = drag_handle_pointer_enter,
	.motion = drag_handle_pointer_motion,
	.button = drag_handle_pointer_button,
	.axis = drag_handle_pointer_axis,
	.cancel = drag_handle_pointer_cancel,
};

uint32_t drag_handle_touch_down(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	// eat the event
	return 0;
}

static void drag_handle_touch_up(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
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

static void drag_handle_touch_motion(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
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

static void drag_handle_touch_enter(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	struct wlr_drag *drag = grab->data;
	drag_set_focus(drag, point->focus_surface, point->sx, point->sy);
}

static void drag_handle_touch_cancel(struct wlr_seat_touch_grab *grab) {
	struct wlr_drag *drag = grab->data;
	drag_end(drag);
}

static const struct wlr_touch_grab_interface
		data_device_touch_drag_interface = {
	.down = drag_handle_touch_down,
	.up = drag_handle_touch_up,
	.motion = drag_handle_touch_motion,
	.enter = drag_handle_touch_enter,
	.cancel = drag_handle_touch_cancel,
};

static void drag_handle_keyboard_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	// nothing has keyboard focus during drags
}

static void drag_handle_keyboard_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	// no keyboard input during drags
}

static void drag_handle_keyboard_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	//struct wlr_keyboard *keyboard = grab->seat->keyboard_state.keyboard;
	// TODO change the dnd action based on what modifier is pressed on the
	// keyboard
}

static void drag_handle_keyboard_cancel(struct wlr_seat_keyboard_grab *grab) {
	struct wlr_drag *drag = grab->data;
	drag_end(drag);
}

static const struct wlr_keyboard_grab_interface
		data_device_keyboard_drag_interface = {
	.enter = drag_handle_keyboard_enter,
	.key = drag_handle_keyboard_key,
	.modifiers = drag_handle_keyboard_modifiers,
	.cancel = drag_handle_keyboard_cancel,
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
	if (icon == NULL) {
		return;
	}
	drag_icon_set_mapped(icon, false);
	wlr_signal_emit_safe(&icon->events.destroy, icon);
	icon->surface->role_data = NULL;
	wl_list_remove(&icon->surface_destroy.link);
	wl_list_remove(&icon->seat_client_destroy.link);
	wl_list_remove(&icon->link);
	free(icon);
}

static void drag_icon_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_drag_icon *icon =
		wl_container_of(listener, icon, surface_destroy);
	drag_icon_destroy(icon);
}

static void drag_icon_surface_role_commit(struct wlr_surface *surface) {
	assert(surface->role == &drag_icon_surface_role);
	struct wlr_drag_icon *icon = surface->role_data;
	if (icon == NULL) {
		return;
	}

	drag_icon_set_mapped(icon, wlr_surface_has_buffer(surface));
}

const struct wlr_surface_role drag_icon_surface_role = {
	.name = "wl_data_device-icon",
	.commit = drag_icon_surface_role_commit,
};

static void drag_icon_handle_seat_client_destroy(struct wl_listener *listener,
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

	wl_signal_init(&icon->events.map);
	wl_signal_init(&icon->events.unmap);
	wl_signal_init(&icon->events.destroy);

	wl_signal_add(&icon->surface->events.destroy, &icon->surface_destroy);
	icon->surface_destroy.notify = drag_icon_handle_surface_destroy;

	wl_signal_add(&client->events.destroy, &icon->seat_client_destroy);
	icon->seat_client_destroy.notify = drag_icon_handle_seat_client_destroy;

	icon->surface->role_data = icon;

	wl_list_insert(&client->seat->drag_icons, &icon->link);
	wlr_signal_emit_safe(&client->seat->events.new_drag_icon, icon);

	if (wlr_surface_has_buffer(icon_surface)) {
		drag_icon_set_mapped(icon, true);
	}

	return icon;
}


static void seat_handle_drag_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, drag_source_destroy);
	wl_list_remove(&seat->drag_source_destroy.link);
	seat->drag_source = NULL;
}

bool seat_client_start_drag(struct wlr_seat_client *client,
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
