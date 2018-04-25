#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "util/signal.h"

static void resource_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void pointer_send_frame(struct wl_resource *resource) {
	if (wl_resource_get_version(resource) >=
			WL_POINTER_FRAME_SINCE_VERSION) {
		wl_pointer_send_frame(resource);
	}
}

static const struct wl_pointer_interface wl_pointer_impl;

static struct wlr_seat_client *seat_client_from_pointer_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_pointer_interface,
		&wl_pointer_impl));
	return wl_resource_get_user_data(resource);
}

static void wl_pointer_set_cursor(struct wl_client *client,
		struct wl_resource *pointer_resource, uint32_t serial,
		struct wl_resource *surface_resource,
		int32_t hotspot_x, int32_t hotspot_y) {
	struct wlr_seat_client *seat_client =
		seat_client_from_pointer_resource(pointer_resource);
	struct wlr_surface *surface = NULL;
	if (surface_resource != NULL) {
		surface = wlr_surface_from_resource(surface_resource);

		if (wlr_surface_set_role(surface, "wl_pointer-cursor", surface_resource,
				WL_POINTER_ERROR_ROLE) < 0) {
			return;
		}
	}

	struct wlr_seat_pointer_request_set_cursor_event *event =
		calloc(1, sizeof(struct wlr_seat_pointer_request_set_cursor_event));
	if (event == NULL) {
		return;
	}
	event->seat_client = seat_client;
	event->surface = surface;
	event->serial = serial;
	event->hotspot_x = hotspot_x;
	event->hotspot_y = hotspot_y;

	wlr_signal_emit_safe(&seat_client->seat->events.request_set_cursor, event);

	free(event);
}

static const struct wl_pointer_interface wl_pointer_impl = {
	.set_cursor = wl_pointer_set_cursor,
	.release = resource_destroy,
};

static void wl_pointer_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void wl_seat_get_pointer(struct wl_client *client,
		struct wl_resource *seat_resource, uint32_t id) {
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	if (!(seat_client->seat->capabilities & WL_SEAT_CAPABILITY_POINTER)) {
		return;
	}

	struct wl_resource *resource = wl_resource_create(client,
		&wl_pointer_interface, wl_resource_get_version(seat_resource), id);
	if (resource == NULL) {
		wl_resource_post_no_memory(seat_resource);
		return;
	}
	wl_resource_set_implementation(resource, &wl_pointer_impl, seat_client,
		&wl_pointer_destroy);
	wl_list_insert(&seat_client->pointers, wl_resource_get_link(resource));
}

static const struct wl_keyboard_interface wl_keyboard_impl = {
	.release = resource_destroy,
};

static void wl_keyboard_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void seat_client_send_keymap(struct wlr_seat_client *client,
		struct wlr_keyboard *keyboard) {
	if (!keyboard) {
		return;
	}

	// TODO: We should probably lift all of the keys set by the other
	// keyboard
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		wl_keyboard_send_keymap(resource,
			WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keyboard->keymap_fd,
			keyboard->keymap_size);
	}
}

static void seat_client_send_repeat_info(struct wlr_seat_client *client,
		struct wlr_keyboard *keyboard) {
	if (!keyboard) {
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		if (wl_resource_get_version(resource) >=
				WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
			wl_keyboard_send_repeat_info(resource,
				keyboard->repeat_info.rate, keyboard->repeat_info.delay);
		}
	}
}

static void wl_seat_get_keyboard(struct wl_client *client,
		struct wl_resource *seat_resource, uint32_t id) {
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	if (!(seat_client->seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) {
		return;
	}

	struct wl_resource *resource = wl_resource_create(client,
		&wl_keyboard_interface, wl_resource_get_version(seat_resource), id);
	if (resource == NULL) {
		wl_resource_post_no_memory(seat_resource);
		return;
	}
	wl_resource_set_implementation(resource, &wl_keyboard_impl, seat_client,
		&wl_keyboard_destroy);
	wl_list_insert(&seat_client->keyboards, wl_resource_get_link(resource));

	struct wlr_keyboard *keyboard = seat_client->seat->keyboard_state.keyboard;
	seat_client_send_keymap(seat_client, keyboard);
	seat_client_send_repeat_info(seat_client, keyboard);

	// TODO possibly handle the case where this keyboard needs an enter
	// right away
}

static const struct wl_touch_interface wl_touch_impl = {
	.release = resource_destroy,
};

static void wl_touch_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void wl_seat_get_touch(struct wl_client *client,
		struct wl_resource *seat_resource, uint32_t id) {
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	if (!(seat_client->seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)) {
		return;
	}

	struct wl_resource *resource = wl_resource_create(client,
		&wl_touch_interface, wl_resource_get_version(seat_resource), id);
	if (resource == NULL) {
		wl_resource_post_no_memory(seat_resource);
		return;
	}
	wl_resource_set_implementation(resource, &wl_touch_impl, seat_client,
		&wl_touch_destroy);
	wl_list_insert(&seat_client->touches, wl_resource_get_link(resource));
}

static void seat_client_resource_destroy(struct wl_resource *seat_resource) {
	struct wlr_seat_client *client =
		wlr_seat_client_from_resource(seat_resource);
	wlr_signal_emit_safe(&client->events.destroy, client);

	if (client == client->seat->pointer_state.focused_client) {
		client->seat->pointer_state.focused_client = NULL;
	}
	if (client == client->seat->keyboard_state.focused_client) {
		client->seat->keyboard_state.focused_client = NULL;
	}

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &client->pointers) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &client->keyboards) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &client->touches) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &client->data_devices) {
		wl_resource_destroy(resource);
	}
	wl_resource_for_each_safe(resource, tmp, &client->primary_selection_devices) {
		wl_resource_destroy(resource);
	}

	wl_list_remove(&client->link);
	free(client);
}

struct wl_seat_interface wl_seat_impl = {
	.get_pointer = wl_seat_get_pointer,
	.get_keyboard = wl_seat_get_keyboard,
	.get_touch = wl_seat_get_touch,
	.release = resource_destroy,
};

static void wl_seat_bind(struct wl_client *client, void *_wlr_seat,
		uint32_t version, uint32_t id) {
	struct wlr_seat *wlr_seat = _wlr_seat;
	assert(client && wlr_seat);

	struct wlr_seat_client *seat_client =
		calloc(1, sizeof(struct wlr_seat_client));
	if (seat_client == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	seat_client->wl_resource =
		wl_resource_create(client, &wl_seat_interface, version, id);
	if (seat_client->wl_resource == NULL) {
		free(seat_client);
		wl_client_post_no_memory(client);
		return;
	}
	seat_client->client = client;
	seat_client->seat = wlr_seat;
	wl_list_init(&seat_client->pointers);
	wl_list_init(&seat_client->keyboards);
	wl_list_init(&seat_client->touches);
	wl_list_init(&seat_client->data_devices);
	wl_list_init(&seat_client->primary_selection_devices);
	wl_resource_set_implementation(seat_client->wl_resource, &wl_seat_impl,
		seat_client, seat_client_resource_destroy);
	wl_list_insert(&wlr_seat->clients, &seat_client->link);
	if (version >= WL_SEAT_NAME_SINCE_VERSION) {
		wl_seat_send_name(seat_client->wl_resource, wlr_seat->name);
	}
	wl_seat_send_capabilities(seat_client->wl_resource, wlr_seat->capabilities);
	wl_signal_init(&seat_client->events.destroy);
}

static void default_pointer_enter(struct wlr_seat_pointer_grab *grab,
		struct wlr_surface *surface, double sx, double sy) {
	wlr_seat_pointer_enter(grab->seat, surface, sx, sy);
}

static void default_pointer_motion(struct wlr_seat_pointer_grab *grab,
		uint32_t time, double sx, double sy) {
	wlr_seat_pointer_send_motion(grab->seat, time, sx, sy);
}

static uint32_t default_pointer_button(struct wlr_seat_pointer_grab *grab,
		uint32_t time, uint32_t button, uint32_t state) {
	return wlr_seat_pointer_send_button(grab->seat, time, button, state);
}

static void default_pointer_axis(struct wlr_seat_pointer_grab *grab,
		uint32_t time, enum wlr_axis_orientation orientation, double value) {
	wlr_seat_pointer_send_axis(grab->seat, time, orientation, value);
}

static void default_pointer_cancel(struct wlr_seat_pointer_grab *grab) {
	// cannot be cancelled
}

static const struct  wlr_pointer_grab_interface default_pointer_grab_impl = {
	.enter = default_pointer_enter,
	.motion = default_pointer_motion,
	.button = default_pointer_button,
	.axis = default_pointer_axis,
	.cancel = default_pointer_cancel,
};

static void default_keyboard_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	wlr_seat_keyboard_enter(grab->seat, surface, keycodes, num_keycodes, modifiers);
}

static void default_keyboard_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	wlr_seat_keyboard_send_key(grab->seat, time, key, state);
}

static void default_keyboard_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	wlr_seat_keyboard_send_modifiers(grab->seat, modifiers);
}

static void default_keyboard_cancel(struct wlr_seat_keyboard_grab *grab) {
	// cannot be cancelled
}

static const struct wlr_keyboard_grab_interface default_keyboard_grab_impl = {
	.enter = default_keyboard_enter,
	.key = default_keyboard_key,
	.modifiers = default_keyboard_modifiers,
	.cancel = default_keyboard_cancel,
};

static uint32_t default_touch_down(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	return wlr_seat_touch_send_down(grab->seat, point->surface, time,
			point->touch_id, point->sx, point->sy);
}

static void default_touch_up(struct wlr_seat_touch_grab *grab, uint32_t time,
		struct wlr_touch_point *point) {
	wlr_seat_touch_send_up(grab->seat, time, point->touch_id);
}

static void default_touch_motion(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	if (!point->focus_surface || point->focus_surface == point->surface) {
		wlr_seat_touch_send_motion(grab->seat, time, point->touch_id, point->sx,
			point->sy);
	}
}

static void default_touch_enter(struct wlr_seat_touch_grab *grab,
		uint32_t time, struct wlr_touch_point *point) {
	// not handled by default
}

static void default_touch_cancel(struct wlr_seat_touch_grab *grab) {
	// cannot be cancelled
}

static const struct wlr_touch_grab_interface default_touch_grab_impl = {
	.down = default_touch_down,
	.up = default_touch_up,
	.motion = default_touch_motion,
	.enter = default_touch_enter,
	.cancel = default_touch_cancel,
};


void wlr_seat_destroy(struct wlr_seat *seat) {
	if (!seat) {
		return;
	}

	wlr_signal_emit_safe(&seat->events.destroy, seat);

	wl_list_remove(&seat->display_destroy.link);

	if (seat->selection_source) {
		wl_list_remove(&seat->selection_source_destroy.link);
		wlr_data_source_cancel(seat->selection_source);
		seat->selection_source = NULL;
	}
	if (seat->primary_selection_source) {
		seat->primary_selection_source->cancel(seat->primary_selection_source);
		seat->primary_selection_source = NULL;
		wl_list_remove(&seat->primary_selection_source_destroy.link);
	}

	struct wlr_seat_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &seat->clients, link) {
		// will destroy other resources as well
		wl_resource_destroy(client->wl_resource);
	}

	wl_global_destroy(seat->wl_global);
	free(seat->pointer_state.default_grab);
	free(seat->keyboard_state.default_grab);
	free(seat->touch_state.default_grab);
	free(seat->name);
	free(seat);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, display_destroy);
	wlr_seat_destroy(seat);
}

struct wlr_seat *wlr_seat_create(struct wl_display *display, const char *name) {
	struct wlr_seat *wlr_seat = calloc(1, sizeof(struct wlr_seat));
	if (!wlr_seat) {
		return NULL;
	}

	// pointer state
	wlr_seat->pointer_state.seat = wlr_seat;
	wl_list_init(&wlr_seat->pointer_state.surface_destroy.link);
	wl_list_init(&wlr_seat->pointer_state.resource_destroy.link);

	struct wlr_seat_pointer_grab *pointer_grab =
		calloc(1, sizeof(struct wlr_seat_pointer_grab));
	if (!pointer_grab) {
		free(wlr_seat);
		return NULL;
	}
	pointer_grab->interface = &default_pointer_grab_impl;
	pointer_grab->seat = wlr_seat;
	wlr_seat->pointer_state.default_grab = pointer_grab;
	wlr_seat->pointer_state.grab = pointer_grab;

	// keyboard state
	struct wlr_seat_keyboard_grab *keyboard_grab =
		calloc(1, sizeof(struct wlr_seat_keyboard_grab));
	if (!keyboard_grab) {
		free(pointer_grab);
		free(wlr_seat);
		return NULL;
	}
	keyboard_grab->interface = &default_keyboard_grab_impl;
	keyboard_grab->seat = wlr_seat;
	wlr_seat->keyboard_state.default_grab = keyboard_grab;
	wlr_seat->keyboard_state.grab = keyboard_grab;

	wlr_seat->keyboard_state.seat = wlr_seat;
	wl_list_init(&wlr_seat->keyboard_state.resource_destroy.link);
	wl_list_init(
		&wlr_seat->keyboard_state.surface_destroy.link);

	// touch state
	struct wlr_seat_touch_grab *touch_grab =
		calloc(1, sizeof(struct wlr_seat_touch_grab));
	if (!touch_grab) {
		free(pointer_grab);
		free(keyboard_grab);
		free(wlr_seat);
		return NULL;
	}
	touch_grab->interface = &default_touch_grab_impl;
	touch_grab->seat = wlr_seat;
	wlr_seat->touch_state.default_grab = touch_grab;
	wlr_seat->touch_state.grab = touch_grab;

	wlr_seat->touch_state.seat = wlr_seat;
	wl_list_init(&wlr_seat->touch_state.touch_points);

	struct wl_global *wl_global = wl_global_create(display,
		&wl_seat_interface, 6, wlr_seat, wl_seat_bind);
	if (!wl_global) {
		free(wlr_seat);
		return NULL;
	}
	wlr_seat->wl_global = wl_global;
	wlr_seat->display = display;
	wlr_seat->name = strdup(name);
	wl_list_init(&wlr_seat->clients);
	wl_list_init(&wlr_seat->drag_icons);

	wl_signal_init(&wlr_seat->events.start_drag);
	wl_signal_init(&wlr_seat->events.new_drag_icon);

	wl_signal_init(&wlr_seat->events.request_set_cursor);

	wl_signal_init(&wlr_seat->events.selection);
	wl_signal_init(&wlr_seat->events.primary_selection);

	wl_signal_init(&wlr_seat->events.pointer_grab_begin);
	wl_signal_init(&wlr_seat->events.pointer_grab_end);

	wl_signal_init(&wlr_seat->events.keyboard_grab_begin);
	wl_signal_init(&wlr_seat->events.keyboard_grab_end);

	wl_signal_init(&wlr_seat->events.touch_grab_begin);
	wl_signal_init(&wlr_seat->events.touch_grab_end);

	wl_signal_init(&wlr_seat->events.destroy);

	wlr_seat->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &wlr_seat->display_destroy);

	return wlr_seat;
}

struct wlr_seat_client *wlr_seat_client_for_wl_client(struct wlr_seat *wlr_seat,
		struct wl_client *wl_client) {
	assert(wlr_seat);
	struct wlr_seat_client *seat_client;
	wl_list_for_each(seat_client, &wlr_seat->clients, link) {
		if (seat_client->client == wl_client) {
			return seat_client;
		}
	}
	return NULL;
}

void wlr_seat_set_capabilities(struct wlr_seat *wlr_seat,
		uint32_t capabilities) {
	wlr_seat->capabilities = capabilities;
	struct wlr_seat_client *client;
	wl_list_for_each(client, &wlr_seat->clients, link) {
		wl_seat_send_capabilities(client->wl_resource, capabilities);
	}
}

void wlr_seat_set_name(struct wlr_seat *wlr_seat, const char *name) {
	free(wlr_seat->name);
	wlr_seat->name = strdup(name);
	struct wlr_seat_client *client;
	wl_list_for_each(client, &wlr_seat->clients, link) {
		wl_seat_send_name(client->wl_resource, name);
	}
}

bool wlr_seat_pointer_surface_has_focus(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface) {
	return surface == wlr_seat->pointer_state.focused_surface;
}

static void pointer_surface_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_pointer_state *state = wl_container_of(
			listener, state, surface_destroy);
	wl_list_remove(&state->surface_destroy.link);
	wl_list_init(&state->surface_destroy.link);
	wlr_seat_pointer_clear_focus(state->seat);
}

static void pointer_resource_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_pointer_state *state = wl_container_of(
			listener, state, resource_destroy);
	wl_list_remove(&state->resource_destroy.link);
	wl_list_init(&state->resource_destroy.link);
	wlr_seat_pointer_clear_focus(state->seat);
}

void wlr_seat_pointer_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy) {
	assert(wlr_seat);

	if (wlr_seat->pointer_state.focused_surface == surface) {
		// this surface already got an enter notify
		return;
	}

	struct wlr_seat_client *client = NULL;
	if (surface) {
		struct wl_client *wl_client = wl_resource_get_client(surface->resource);
		client = wlr_seat_client_for_wl_client(wlr_seat, wl_client);
	}

	struct wlr_seat_client *focused_client =
		wlr_seat->pointer_state.focused_client;
	struct wlr_surface *focused_surface =
		wlr_seat->pointer_state.focused_surface;

	// leave the previously entered surface
	if (focused_client != NULL && focused_surface != NULL) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &focused_client->pointers) {
			wl_pointer_send_leave(resource, serial, focused_surface->resource);
			pointer_send_frame(resource);
		}
	}

	// enter the current surface
	if (client != NULL && surface != NULL) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &client->pointers) {
			wl_pointer_send_enter(resource, serial, surface->resource,
				wl_fixed_from_double(sx), wl_fixed_from_double(sy));
			pointer_send_frame(resource);
		}
	}

	// reinitialize the focus destroy events
	wl_list_remove(&wlr_seat->pointer_state.surface_destroy.link);
	wl_list_init(&wlr_seat->pointer_state.surface_destroy.link);
	wl_list_remove(&wlr_seat->pointer_state.resource_destroy.link);
	wl_list_init(&wlr_seat->pointer_state.resource_destroy.link);
	if (surface != NULL) {
		wl_signal_add(&surface->events.destroy,
			&wlr_seat->pointer_state.surface_destroy);
		wl_resource_add_destroy_listener(surface->resource,
			&wlr_seat->pointer_state.resource_destroy);
		wlr_seat->pointer_state.resource_destroy.notify =
			pointer_resource_destroy_notify;
		wlr_seat->pointer_state.surface_destroy.notify =
			pointer_surface_destroy_notify;
	}

	wlr_seat->pointer_state.focused_client = client;
	wlr_seat->pointer_state.focused_surface = surface;

	// TODO: send focus change event
}

void wlr_seat_pointer_clear_focus(struct wlr_seat *wlr_seat) {
	wlr_seat_pointer_enter(wlr_seat, NULL, 0, 0);
}

void wlr_seat_pointer_send_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy) {
	struct wlr_seat_client *client = wlr_seat->pointer_state.focused_client;
	if (client == NULL) {
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->pointers) {
		wl_pointer_send_motion(resource, time, wl_fixed_from_double(sx),
			wl_fixed_from_double(sy));
		pointer_send_frame(resource);
	}
}

uint32_t wlr_seat_pointer_send_button(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t button, uint32_t state) {
	struct wlr_seat_client *client = wlr_seat->pointer_state.focused_client;
	if (client == NULL) {
		return 0;
	}

	uint32_t serial = wl_display_next_serial(wlr_seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->pointers) {
		wl_pointer_send_button(resource, serial, time, button, state);
		pointer_send_frame(resource);
	}
	return serial;
}

void wlr_seat_pointer_send_axis(struct wlr_seat *wlr_seat, uint32_t time,
		enum wlr_axis_orientation orientation, double value) {
	struct wlr_seat_client *client = wlr_seat->pointer_state.focused_client;
	if (client == NULL) {
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->pointers) {
		if (value) {
			wl_pointer_send_axis(resource, time, orientation,
				wl_fixed_from_double(value));
		} else if (wl_resource_get_version(resource) >=
				WL_POINTER_AXIS_STOP_SINCE_VERSION) {
			wl_pointer_send_axis_stop(resource, time, orientation);
		}
		pointer_send_frame(resource);
	}
}

void wlr_seat_pointer_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_pointer_grab *grab) {
	assert(wlr_seat);
	grab->seat = wlr_seat;
	assert(grab->seat);
	wlr_seat->pointer_state.grab = grab;

	wlr_signal_emit_safe(&wlr_seat->events.pointer_grab_begin, grab);
}

void wlr_seat_pointer_end_grab(struct wlr_seat *wlr_seat) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	if (grab != wlr_seat->pointer_state.default_grab) {
		wlr_seat->pointer_state.grab = wlr_seat->pointer_state.default_grab;
		wlr_signal_emit_safe(&wlr_seat->events.pointer_grab_end, grab);
		if (grab->interface->cancel) {
			grab->interface->cancel(grab);
		}
	}
}

void wlr_seat_pointer_notify_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	grab->interface->enter(grab, surface, sx, sy);
}

void wlr_seat_pointer_notify_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy) {
	clock_gettime(CLOCK_MONOTONIC, &wlr_seat->last_event);
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	grab->interface->motion(grab, time, sx, sy);
}

uint32_t wlr_seat_pointer_notify_button(struct wlr_seat *wlr_seat,
		uint32_t time, uint32_t button, uint32_t state) {
	clock_gettime(CLOCK_MONOTONIC, &wlr_seat->last_event);
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (wlr_seat->pointer_state.button_count == 0) {
			wlr_seat->pointer_state.grab_button = button;
			wlr_seat->pointer_state.grab_time = time;
		}
		wlr_seat->pointer_state.button_count++;
	} else {
		wlr_seat->pointer_state.button_count--;
	}

	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	uint32_t serial = grab->interface->button(grab, time, button, state);

	if (serial && wlr_seat->pointer_state.button_count == 1) {
		wlr_seat->pointer_state.grab_serial = serial;
	}

	return serial;
}

void wlr_seat_pointer_notify_axis(struct wlr_seat *wlr_seat, uint32_t time,
		enum wlr_axis_orientation orientation, double value) {
	clock_gettime(CLOCK_MONOTONIC, &wlr_seat->last_event);
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	grab->interface->axis(grab, time, orientation, value);
}

bool wlr_seat_pointer_has_grab(struct wlr_seat *seat) {
	return seat->pointer_state.grab->interface != &default_pointer_grab_impl;
}

void wlr_seat_keyboard_send_key(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t key, uint32_t state) {
	struct wlr_seat_client *client = wlr_seat->keyboard_state.focused_client;
	if (!client) {
		return;
	}

	uint32_t serial = wl_display_next_serial(wlr_seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		wl_keyboard_send_key(resource, serial, time, key, state);
	}
}

static void handle_keyboard_keymap(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard_state *state =
		wl_container_of(listener, state, keyboard_keymap);
	struct wlr_seat_client *client;
	struct wlr_keyboard *keyboard = data;
	if (keyboard == state->keyboard) {
		wl_list_for_each(client, &state->seat->clients, link) {
			seat_client_send_keymap(client, state->keyboard);
		}
	}
}

static void handle_keyboard_repeat_info(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state =
		wl_container_of(listener, state, keyboard_repeat_info);
	struct wlr_seat_client *client;
	wl_list_for_each(client, &state->seat->clients, link) {
		seat_client_send_repeat_info(client, state->keyboard);
	}
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard_state *state =
		wl_container_of(listener, state, keyboard_destroy);
	state->keyboard = NULL;
}

void wlr_seat_set_keyboard(struct wlr_seat *seat,
		struct wlr_input_device *device) {
	// TODO call this on device key event before the event reaches the
	// compositor and set a pending keyboard and then send the new keyboard
	// state on the next keyboard notify event.
	struct wlr_keyboard *keyboard = (device ? device->keyboard : NULL);
	if (seat->keyboard_state.keyboard == keyboard) {
		return;
	}

	if (seat->keyboard_state.keyboard) {
		wl_list_remove(&seat->keyboard_state.keyboard_destroy.link);
		wl_list_remove(&seat->keyboard_state.keyboard_keymap.link);
		wl_list_remove(&seat->keyboard_state.keyboard_repeat_info.link);
		seat->keyboard_state.keyboard = NULL;
	}

	if (keyboard) {
		assert(device->type == WLR_INPUT_DEVICE_KEYBOARD);
		seat->keyboard_state.keyboard = keyboard;

		wl_signal_add(&device->events.destroy,
			&seat->keyboard_state.keyboard_destroy);
		seat->keyboard_state.keyboard_destroy.notify = handle_keyboard_destroy;
		wl_signal_add(&device->keyboard->events.keymap,
			&seat->keyboard_state.keyboard_keymap);
		seat->keyboard_state.keyboard_keymap.notify = handle_keyboard_keymap;
		wl_signal_add(&device->keyboard->events.repeat_info,
			&seat->keyboard_state.keyboard_repeat_info);
		seat->keyboard_state.keyboard_repeat_info.notify =
			handle_keyboard_repeat_info;

		struct wlr_seat_client *client;
		wl_list_for_each(client, &seat->clients, link) {
			seat_client_send_keymap(client, keyboard);
			seat_client_send_repeat_info(client, keyboard);
		}

		wlr_seat_keyboard_send_modifiers(seat, &keyboard->modifiers);
	} else {
		seat->keyboard_state.keyboard = NULL;
	}
}

struct wlr_keyboard *wlr_seat_get_keyboard(struct wlr_seat *seat) {
	return seat->keyboard_state.keyboard;
}

void wlr_seat_keyboard_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_keyboard_grab *grab) {
	grab->seat = wlr_seat;
	wlr_seat->keyboard_state.grab = grab;

	wlr_signal_emit_safe(&wlr_seat->events.keyboard_grab_begin, grab);
}

void wlr_seat_keyboard_end_grab(struct wlr_seat *wlr_seat) {
	struct wlr_seat_keyboard_grab *grab = wlr_seat->keyboard_state.grab;

	if (grab != wlr_seat->keyboard_state.default_grab) {
		wlr_seat->keyboard_state.grab = wlr_seat->keyboard_state.default_grab;
		wlr_signal_emit_safe(&wlr_seat->events.keyboard_grab_end, grab);
		if (grab->interface->cancel) {
			grab->interface->cancel(grab);
		}
	}
}

static void keyboard_surface_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state = wl_container_of(
			listener, state, surface_destroy);
	wl_list_remove(&state->surface_destroy.link);
	wl_list_init(&state->surface_destroy.link);
	wlr_seat_keyboard_clear_focus(state->seat);
}

static void keyboard_resource_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state = wl_container_of(
			listener, state, resource_destroy);
	wl_list_remove(&state->resource_destroy.link);
	wl_list_init(&state->resource_destroy.link);
	wlr_seat_keyboard_clear_focus(state->seat);
}

void wlr_seat_keyboard_send_modifiers(struct wlr_seat *seat,
		struct wlr_keyboard_modifiers *modifiers) {
	struct wlr_seat_client *client = seat->keyboard_state.focused_client;
	if (client == NULL) {
		return;
	}

	uint32_t serial = wl_display_next_serial(seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		if (modifiers == NULL) {
			wl_keyboard_send_modifiers(resource, serial, 0, 0, 0, 0);
		} else {
			wl_keyboard_send_modifiers(resource, serial,
				modifiers->depressed, modifiers->latched,
				modifiers->locked, modifiers->group);
		}
	}
}

void wlr_seat_keyboard_enter(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	if (seat->keyboard_state.focused_surface == surface) {
		// this surface already got an enter notify
		return;
	}

	struct wlr_seat_client *client = NULL;

	if (surface) {
		struct wl_client *wl_client = wl_resource_get_client(surface->resource);
		client = wlr_seat_client_for_wl_client(seat, wl_client);
	}

	struct wlr_seat_client *focused_client =
		seat->keyboard_state.focused_client;
	struct wlr_surface *focused_surface =
		seat->keyboard_state.focused_surface;

	// leave the previously entered surface
	if (focused_client != NULL && focused_surface != NULL) {
		uint32_t serial = wl_display_next_serial(seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &focused_client->keyboards) {
			wl_keyboard_send_leave(resource, serial, focused_surface->resource);
		}
	}

	// enter the current surface
	if (client != NULL) {
		struct wl_array keys;
		wl_array_init(&keys);
		for (size_t i = 0; i < num_keycodes; ++i) {
			uint32_t *p = wl_array_add(&keys, sizeof(uint32_t));
			if (!p) {
				wlr_log(L_ERROR, "Cannot allocate memory, skipping keycode: %d\n",
					keycodes[i]);
				continue;
			}
			*p = keycodes[i];
		}
		uint32_t serial = wl_display_next_serial(seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &client->keyboards) {
			wl_keyboard_send_enter(resource, serial, surface->resource, &keys);
		}
		wl_array_release(&keys);

		wlr_seat_client_send_selection(client);
		wlr_seat_client_send_primary_selection(client);
	}

	// reinitialize the focus destroy events
	wl_list_remove(&seat->keyboard_state.surface_destroy.link);
	wl_list_init(&seat->keyboard_state.surface_destroy.link);
	wl_list_remove(&seat->keyboard_state.resource_destroy.link);
	wl_list_init(&seat->keyboard_state.resource_destroy.link);
	if (surface) {
		wl_signal_add(&surface->events.destroy,
			&seat->keyboard_state.surface_destroy);
		wl_resource_add_destroy_listener(surface->resource,
			&seat->keyboard_state.resource_destroy);
		seat->keyboard_state.resource_destroy.notify =
			keyboard_resource_destroy_notify;
		seat->keyboard_state.surface_destroy.notify =
			keyboard_surface_destroy_notify;
	}

	seat->keyboard_state.focused_client = client;
	seat->keyboard_state.focused_surface = surface;

	if (client != NULL) {
		// tell new client about any modifier change last,
		// as it targets seat->keyboard_state.focused_client
		wlr_seat_keyboard_send_modifiers(seat, modifiers);
	}
}

void wlr_seat_keyboard_notify_enter(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->enter(grab, surface, keycodes, num_keycodes, modifiers);
}

void wlr_seat_keyboard_clear_focus(struct wlr_seat *seat) {
	// TODO respect grabs here?
	wlr_seat_keyboard_enter(seat, NULL, NULL, 0, NULL);
}

bool wlr_seat_keyboard_has_grab(struct wlr_seat *seat) {
	return seat->keyboard_state.grab->interface != &default_keyboard_grab_impl;
}

void wlr_seat_keyboard_notify_modifiers(struct wlr_seat *seat,
		struct wlr_keyboard_modifiers *modifiers) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->modifiers(grab, modifiers);
}

void wlr_seat_keyboard_notify_key(struct wlr_seat *seat, uint32_t time,
		uint32_t key, uint32_t state) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->key(grab, time, key, state);
}

void wlr_seat_touch_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_touch_grab *grab) {
	grab->seat = wlr_seat;
	wlr_seat->touch_state.grab = grab;

	wlr_signal_emit_safe(&wlr_seat->events.touch_grab_begin, grab);
}

void wlr_seat_touch_end_grab(struct wlr_seat *wlr_seat) {
	struct wlr_seat_touch_grab *grab = wlr_seat->touch_state.grab;

	if (grab != wlr_seat->touch_state.default_grab) {
		wlr_seat->touch_state.grab = wlr_seat->touch_state.default_grab;
		wlr_signal_emit_safe(&wlr_seat->events.touch_grab_end, grab);
		if (grab->interface->cancel) {
			grab->interface->cancel(grab);
		}
	}
}

static void touch_point_clear_focus(struct wlr_touch_point *point) {
	if (point->focus_surface) {
		wl_list_remove(&point->focus_surface_destroy.link);
		point->focus_client = NULL;
		point->focus_surface = NULL;
	}
}

static void touch_point_destroy(struct wlr_touch_point *point) {
	wlr_signal_emit_safe(&point->events.destroy, point);

	touch_point_clear_focus(point);
	wl_list_remove(&point->surface_destroy.link);
	wl_list_remove(&point->resource_destroy.link);
	wl_list_remove(&point->link);
	free(point);
}
static void handle_touch_point_resource_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_touch_point *point =
		wl_container_of(listener, point, resource_destroy);
	touch_point_destroy(point);
}

static void handle_touch_point_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_touch_point *point =
		wl_container_of(listener, point, surface_destroy);
	touch_point_destroy(point);
}

static struct wlr_touch_point *touch_point_create(
		struct wlr_seat *seat, int32_t touch_id,
		struct wlr_surface *surface, double sx, double sy) {
	struct wl_client *wl_client = wl_resource_get_client(surface->resource);
	struct wlr_seat_client *client = wlr_seat_client_for_wl_client(seat, wl_client);

	if (client == NULL || wl_list_empty(&client->touches)) {
		// touch points are not valid without a connected client with touch
		return NULL;
	}

	struct wlr_touch_point *point = calloc(1, sizeof(struct wlr_touch_point));
	if (!point) {
		return NULL;
	}

	point->touch_id = touch_id;
	point->surface = surface;
	point->client = client;

	point->sx = sx;
	point->sy = sy;

	wl_signal_init(&point->events.destroy);

	wl_signal_add(&surface->events.destroy, &point->surface_destroy);
	point->surface_destroy.notify = handle_touch_point_surface_destroy;
	wl_resource_add_destroy_listener(surface->resource,
		&point->resource_destroy);
	point->resource_destroy.notify = handle_touch_point_resource_destroy;

	wl_list_insert(&seat->touch_state.touch_points, &point->link);

	return point;
}

struct wlr_touch_point *wlr_seat_touch_get_point(
		struct wlr_seat *seat, int32_t touch_id) {
	struct wlr_touch_point *point = NULL;
	wl_list_for_each(point, &seat->touch_state.touch_points, link) {
		if (point->touch_id == touch_id) {
			return point;
		}
	}

	return NULL;
}

uint32_t wlr_seat_touch_notify_down(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t time, int32_t touch_id, double sx,
		double sy) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_touch_grab *grab = seat->touch_state.grab;
	struct wlr_touch_point *point =
		touch_point_create(seat, touch_id, surface, sx, sy);
	if (!point) {
		wlr_log(L_ERROR, "could not create touch point");
		return 0;
	}

	uint32_t serial = grab->interface->down(grab, time, point);

	if (serial && wlr_seat_touch_num_points(seat) == 1) {
		seat->touch_state.grab_serial = serial;
		seat->touch_state.grab_id = touch_id;
	}

	return serial;
}

void wlr_seat_touch_notify_up(struct wlr_seat *seat, uint32_t time,
		int32_t touch_id) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_touch_grab *grab = seat->touch_state.grab;
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch up for unknown touch point");
		return;
	}

	grab->interface->up(grab, time, point);

	touch_point_destroy(point);
}

void wlr_seat_touch_notify_motion(struct wlr_seat *seat, uint32_t time,
		int32_t touch_id, double sx, double sy) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_touch_grab *grab = seat->touch_state.grab;
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch motion for unknown touch point");
		return;
	}

	point->sx = sx;
	point->sy = sy;

	grab->interface->motion(grab, time, point);
}

static void handle_point_focus_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_touch_point *point =
		wl_container_of(listener, point, focus_surface_destroy);
	touch_point_clear_focus(point);
}

static void touch_point_set_focus(struct wlr_touch_point *point,
		struct wlr_surface *surface, double sx, double sy) {
	if (point->focus_surface == surface) {
		return;
	}

	touch_point_clear_focus(point);

	if (surface && surface->resource) {
		struct wlr_seat_client *client =
			wlr_seat_client_for_wl_client(point->client->seat,
				wl_resource_get_client(surface->resource));

		if (client && !wl_list_empty(&client->touches)) {
			wl_signal_add(&surface->events.destroy, &point->focus_surface_destroy);
			point->focus_surface_destroy.notify = handle_point_focus_destroy;
			point->focus_surface = surface;
			point->focus_client = client;
			point->sx = sx;
			point->sy = sy;
		}
	}
}

void wlr_seat_touch_point_focus(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t time, int32_t touch_id, double sx,
		double sy) {
	assert(surface);
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch point focus for unknown touch point");
		return;
	}
	struct wlr_surface *focus = point->focus_surface;
	touch_point_set_focus(point, surface, sx, sy);

	if (focus != point->focus_surface) {
		struct wlr_seat_touch_grab *grab = seat->touch_state.grab;
		grab->interface->enter(grab, time, point);
	}
}

void wlr_seat_touch_point_clear_focus(struct wlr_seat *seat, uint32_t time,
		int32_t touch_id) {
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch point focus for unknown touch point");
		return;
	}

	touch_point_clear_focus(point);
}

uint32_t wlr_seat_touch_send_down(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t time, int32_t touch_id, double sx,
		double sy) {
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch down for unknown touch point");
		return 0;
	}

	uint32_t serial = wl_display_next_serial(seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &point->client->touches) {
		wl_touch_send_down(resource, serial, time, surface->resource,
			touch_id, wl_fixed_from_double(sx), wl_fixed_from_double(sy));
		wl_touch_send_frame(resource);
	}

	return serial;
}

void wlr_seat_touch_send_up(struct wlr_seat *seat, uint32_t time, int32_t touch_id) {
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch up for unknown touch point");
		return;
	}

	uint32_t serial = wl_display_next_serial(seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &point->client->touches) {
		wl_touch_send_up(resource, serial, time, touch_id);
		wl_touch_send_frame(resource);
	}
}

void wlr_seat_touch_send_motion(struct wlr_seat *seat, uint32_t time, int32_t touch_id,
		double sx, double sy) {
	struct wlr_touch_point *point = wlr_seat_touch_get_point(seat, touch_id);
	if (!point) {
		wlr_log(L_ERROR, "got touch motion for unknown touch point");
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &point->client->touches) {
		wl_touch_send_motion(resource, time, touch_id, wl_fixed_from_double(sx),
			wl_fixed_from_double(sy));
		wl_touch_send_frame(resource);
	}
}

int wlr_seat_touch_num_points(struct wlr_seat *seat) {
	return wl_list_length(&seat->touch_state.touch_points);
}

bool wlr_seat_touch_has_grab(struct wlr_seat *seat) {
	return seat->touch_state.grab->interface != &default_touch_grab_impl;
}

bool wlr_seat_validate_grab_serial(struct wlr_seat *seat, uint32_t serial) {
	return true;
	//return serial == seat->pointer_state.grab_serial ||
	//	serial == seat->touch_state.grab_serial;
}

struct wlr_seat_client *wlr_seat_client_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_seat_interface,
		&wl_seat_impl));
	return wl_resource_get_user_data(resource);
}
