#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>

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

static void wl_pointer_set_cursor(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial,
		struct wl_resource *surface_resource,
		int32_t hotspot_x, int32_t hotspot_y) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	struct wlr_surface *surface = NULL;
	if (surface_resource != NULL) {
		surface = wl_resource_get_user_data(surface_resource);

		if (wlr_surface_set_role(surface, "wl_pointer-cursor", resource,
				WL_POINTER_ERROR_ROLE) < 0) {
			return;
		}
	}

	struct wlr_seat_pointer_request_set_cursor_event *event =
		calloc(1, sizeof(struct wlr_seat_pointer_request_set_cursor_event));
	if (event == NULL) {
		return;
	}
	event->client = client;
	event->seat_handle = handle;
	event->surface = surface;
	event->hotspot_x = hotspot_x;
	event->hotspot_y = hotspot_y;

	wl_signal_emit(&handle->wlr_seat->events.request_set_cursor, event);

	free(event);
}

static const struct wl_pointer_interface wl_pointer_impl = {
	.set_cursor = wl_pointer_set_cursor,
	.release = resource_destroy
};

static void wl_pointer_destroy(struct wl_resource *resource) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	if (handle->pointer) {
		handle->pointer = NULL;
	}
}

static void wl_seat_get_pointer(struct wl_client *client,
		struct wl_resource *_handle, uint32_t id) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(_handle);
	if (!(handle->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER)) {
		return;
	}
	if (handle->pointer) {
		// TODO: this is probably a protocol violation but it simplifies our
		// code and it'd be stupid for clients to create several pointers for
		// the same seat
		wl_resource_destroy(handle->pointer);
	}
	handle->pointer = wl_resource_create(client, &wl_pointer_interface,
		wl_resource_get_version(_handle), id);
	wl_resource_set_implementation(handle->pointer, &wl_pointer_impl,
		handle, &wl_pointer_destroy);
}

static const struct wl_keyboard_interface wl_keyboard_impl = {
	.release = resource_destroy
};

static void wl_keyboard_destroy(struct wl_resource *resource) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	if (handle->keyboard) {
		handle->keyboard = NULL;
	}
}

static void wl_seat_get_keyboard(struct wl_client *client,
		struct wl_resource *_handle, uint32_t id) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(_handle);
	if (!(handle->wlr_seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) {
		return;
	}
	if (handle->keyboard) {
		// TODO: this is probably a protocol violation but it simplifies our
		// code and it'd be stupid for clients to create several keyboards for
		// the same seat
		wl_resource_destroy(handle->keyboard);
	}
	handle->keyboard = wl_resource_create(client, &wl_keyboard_interface,
		wl_resource_get_version(_handle), id);
	wl_resource_set_implementation(handle->keyboard, &wl_keyboard_impl,
		handle, &wl_keyboard_destroy);
}

static const struct wl_touch_interface wl_touch_impl = {
	.release = resource_destroy
};

static void wl_touch_destroy(struct wl_resource *resource) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	if (handle->touch) {
		handle->touch = NULL;
	}
}

static void wl_seat_get_touch(struct wl_client *client,
		struct wl_resource *_handle, uint32_t id) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(_handle);
	if (!(handle->wlr_seat->capabilities & WL_SEAT_CAPABILITY_TOUCH)) {
		return;
	}
	if (handle->touch) {
		// TODO: this is probably a protocol violation but it simplifies our
		// code and it'd be stupid for clients to create several pointers for
		// the same seat
		wl_resource_destroy(handle->touch);
	}
	handle->touch = wl_resource_create(client, &wl_touch_interface,
		wl_resource_get_version(_handle), id);
	wl_resource_set_implementation(handle->touch, &wl_touch_impl,
		handle, &wl_touch_destroy);
}

static void wlr_seat_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	if (handle == handle->wlr_seat->pointer_state.focused_handle) {
		handle->wlr_seat->pointer_state.focused_handle = NULL;
	}
	if (handle == handle->wlr_seat->keyboard_state.focused_handle) {
		handle->wlr_seat->keyboard_state.focused_handle = NULL;
	}

	if (handle->pointer) {
		wl_resource_destroy(handle->pointer);
	}
	if (handle->keyboard) {
		wl_resource_destroy(handle->keyboard);
	}
	if (handle->touch) {
		wl_resource_destroy(handle->touch);
	}
	if (handle->data_device) {
		wl_resource_destroy(handle->data_device);
	}
	wl_signal_emit(&handle->wlr_seat->events.client_unbound, handle);
	wl_list_remove(&handle->link);
	free(handle);
}

struct wl_seat_interface wl_seat_impl = {
	.get_pointer = wl_seat_get_pointer,
	.get_keyboard = wl_seat_get_keyboard,
	.get_touch = wl_seat_get_touch,
	.release = resource_destroy
};

static void wl_seat_bind(struct wl_client *wl_client, void *_wlr_seat,
		uint32_t version, uint32_t id) {
	struct wlr_seat *wlr_seat = _wlr_seat;
	assert(wl_client && wlr_seat);
	if (version > 6) {
		wlr_log(L_ERROR,
			"Client requested unsupported wl_seat version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wlr_seat_handle *handle = calloc(1, sizeof(struct wlr_seat_handle));
	handle->wl_resource = wl_resource_create(
			wl_client, &wl_seat_interface, version, id);
	handle->wlr_seat = wlr_seat;
	wl_resource_set_implementation(handle->wl_resource, &wl_seat_impl,
		handle, wlr_seat_handle_resource_destroy);
	wl_list_insert(&wlr_seat->handles, &handle->link);
	if (version >= WL_SEAT_NAME_SINCE_VERSION) {
		wl_seat_send_name(handle->wl_resource, wlr_seat->name);
	}
	wl_seat_send_capabilities(handle->wl_resource, wlr_seat->capabilities);
	wl_signal_emit(&wlr_seat->events.client_bound, handle);
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
		struct wlr_surface *surface) {
	wlr_seat_keyboard_enter(grab->seat, surface);
}

static void default_keyboard_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	wlr_seat_keyboard_send_key(grab->seat, time, key, state);
}

static void default_keyboard_modifiers(struct wlr_seat_keyboard_grab *grab,
		uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	wlr_seat_keyboard_send_modifiers(grab->seat, mods_depressed,
		mods_latched, mods_locked, group);
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

struct wlr_seat *wlr_seat_create(struct wl_display *display, const char *name) {
	struct wlr_seat *wlr_seat = calloc(1, sizeof(struct wlr_seat));
	if (!wlr_seat) {
		return NULL;
	}

	wlr_seat->pointer_state.wlr_seat = wlr_seat;
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

	wlr_seat->keyboard_state.wlr_seat = wlr_seat;
	wl_list_init(&wlr_seat->keyboard_state.resource_destroy.link);
	wl_list_init(
		&wlr_seat->keyboard_state.surface_destroy.link);

	struct wl_global *wl_global = wl_global_create(display,
		&wl_seat_interface, 6, wlr_seat, wl_seat_bind);
	if (!wl_global) {
		free(wlr_seat);
		return NULL;
	}
	wlr_seat->wl_global = wl_global;
	wlr_seat->display = display;
	wlr_seat->name = strdup(name);
	wl_list_init(&wlr_seat->handles);
	wl_list_init(&wlr_seat->keyboards);

	wl_signal_init(&wlr_seat->events.client_bound);
	wl_signal_init(&wlr_seat->events.client_unbound);
	wl_signal_init(&wlr_seat->events.request_set_cursor);

	wl_signal_init(&wlr_seat->events.pointer_grab_begin);
	wl_signal_init(&wlr_seat->events.pointer_grab_end);

	wl_signal_init(&wlr_seat->events.keyboard_grab_begin);
	wl_signal_init(&wlr_seat->events.keyboard_grab_end);

	return wlr_seat;
}

void wlr_seat_destroy(struct wlr_seat *wlr_seat) {
	if (!wlr_seat) {
		return;
	}

	struct wlr_seat_handle *handle, *tmp;
	wl_list_for_each_safe(handle, tmp, &wlr_seat->handles, link) {
		// will destroy other resources as well
		wl_resource_destroy(handle->wl_resource);
	}

	wl_global_destroy(wlr_seat->wl_global);
	free(wlr_seat->pointer_state.default_grab);
	free(wlr_seat->keyboard_state.default_grab);
	free(wlr_seat->data_device);
	free(wlr_seat->name);
	free(wlr_seat);
}

struct wlr_seat_handle *wlr_seat_handle_for_client(struct wlr_seat *wlr_seat,
		struct wl_client *client) {
	assert(wlr_seat);
	struct wlr_seat_handle *handle;
	wl_list_for_each(handle, &wlr_seat->handles, link) {
		if (wl_resource_get_client(handle->wl_resource) == client) {
			return handle;
		}
	}
	return NULL;
}

void wlr_seat_set_capabilities(struct wlr_seat *wlr_seat,
		uint32_t capabilities) {
	wlr_seat->capabilities = capabilities;
	struct wlr_seat_handle *handle;
	wl_list_for_each(handle, &wlr_seat->handles, link) {
		wl_seat_send_capabilities(handle->wl_resource, capabilities);
	}
}

void wlr_seat_set_name(struct wlr_seat *wlr_seat, const char *name) {
	free(wlr_seat->name);
	wlr_seat->name = strdup(name);
	struct wlr_seat_handle *handle;
	wl_list_for_each(handle, &wlr_seat->handles, link) {
		wl_seat_send_name(handle->wl_resource, name);
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
	state->focused_surface = NULL;
	wlr_seat_pointer_clear_focus(state->wlr_seat);
}

static void pointer_resource_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_pointer_state *state = wl_container_of(
			listener, state, resource_destroy);
	wl_list_remove(&state->resource_destroy.link);
	wl_list_init(&state->resource_destroy.link);
	state->focused_surface = NULL;
	wlr_seat_pointer_clear_focus(state->wlr_seat);
}

static bool wlr_seat_pointer_has_focus_resource(struct wlr_seat *wlr_seat) {
	return wlr_seat->pointer_state.focused_handle &&
		wlr_seat->pointer_state.focused_handle->pointer;
}

void wlr_seat_pointer_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy) {
	assert(wlr_seat);

	if (wlr_seat->pointer_state.focused_surface == surface) {
		// this surface already got an enter notify
		return;
	}

	struct wlr_seat_handle *handle = NULL;

	if (surface) {
		struct wl_client *client = wl_resource_get_client(surface->resource);
		handle = wlr_seat_handle_for_client(wlr_seat, client);
	}

	struct wlr_seat_handle *focused_handle =
		wlr_seat->pointer_state.focused_handle;
	struct wlr_surface *focused_surface =
		wlr_seat->pointer_state.focused_surface;

	// leave the previously entered surface
	if (focused_handle && focused_handle->pointer && focused_surface) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_pointer_send_leave(focused_handle->pointer, serial,
			focused_surface->resource);
		pointer_send_frame(focused_handle->pointer);
	}

	// enter the current surface
	if (handle && handle->pointer) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_pointer_send_enter(handle->pointer, serial, surface->resource,
			wl_fixed_from_double(sx), wl_fixed_from_double(sy));
		pointer_send_frame(handle->pointer);
	}

	// reinitialize the focus destroy events
	wl_list_remove(&wlr_seat->pointer_state.surface_destroy.link);
	wl_list_init(&wlr_seat->pointer_state.surface_destroy.link);
	wl_list_remove(&wlr_seat->pointer_state.resource_destroy.link);
	wl_list_init(&wlr_seat->pointer_state.resource_destroy.link);
	if (surface) {
		wl_signal_add(&surface->events.destroy,
			&wlr_seat->pointer_state.surface_destroy);
		wl_resource_add_destroy_listener(surface->resource,
			&wlr_seat->pointer_state.resource_destroy);
		wlr_seat->pointer_state.resource_destroy.notify =
			pointer_resource_destroy_notify;
		wlr_seat->pointer_state.surface_destroy.notify =
			pointer_surface_destroy_notify;
	}

	wlr_seat->pointer_state.focused_handle = handle;
	wlr_seat->pointer_state.focused_surface = surface;

	// TODO: send focus change event
}

void wlr_seat_pointer_clear_focus(struct wlr_seat *wlr_seat) {
	wlr_seat_pointer_enter(wlr_seat, NULL, 0, 0);
}

void wlr_seat_pointer_send_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy) {
	if (!wlr_seat_pointer_has_focus_resource(wlr_seat)) {
		return;
	}

	wl_pointer_send_motion(wlr_seat->pointer_state.focused_handle->pointer,
		time, wl_fixed_from_double(sx), wl_fixed_from_double(sy));
	pointer_send_frame(wlr_seat->pointer_state.focused_handle->pointer);
}

uint32_t wlr_seat_pointer_send_button(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t button, uint32_t state) {
	if (!wlr_seat_pointer_has_focus_resource(wlr_seat)) {
		return 0;
	}

	uint32_t serial = wl_display_next_serial(wlr_seat->display);
	wl_pointer_send_button(wlr_seat->pointer_state.focused_handle->pointer,
		serial, time, button, state);
	pointer_send_frame(wlr_seat->pointer_state.focused_handle->pointer);
	return serial;
}

void wlr_seat_pointer_send_axis(struct wlr_seat *wlr_seat, uint32_t time,
		enum wlr_axis_orientation orientation, double value) {
	if (!wlr_seat_pointer_has_focus_resource(wlr_seat)) {
		return;
	}

	struct wl_resource *pointer =
		wlr_seat->pointer_state.focused_handle->pointer;

	if (value) {
		wl_pointer_send_axis(pointer, time, orientation,
			wl_fixed_from_double(value));
	} else if (wl_resource_get_version(pointer) >=
			WL_POINTER_AXIS_STOP_SINCE_VERSION) {
		wl_pointer_send_axis_stop(pointer, time, orientation);
	}

	pointer_send_frame(pointer);
}

void wlr_seat_pointer_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_pointer_grab *grab) {
	grab->seat = wlr_seat;
	wlr_seat->pointer_state.grab = grab;

	wl_signal_emit(&wlr_seat->events.pointer_grab_begin, grab);
}

void wlr_seat_pointer_end_grab(struct wlr_seat *wlr_seat) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	if (grab != wlr_seat->pointer_state.default_grab) {
		wlr_seat->pointer_state.grab = wlr_seat->pointer_state.default_grab;
		wl_signal_emit(&wlr_seat->events.pointer_grab_end, grab);
	}
}

void wlr_seat_pointer_notify_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	grab->interface->enter(grab, surface, sx, sy);
}

void wlr_seat_pointer_notify_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	grab->interface->motion(grab, time, sx, sy);
}

uint32_t wlr_seat_pointer_notify_button(struct wlr_seat *wlr_seat,
		uint32_t time, uint32_t button, uint32_t state) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	return grab->interface->button(grab, time, button, state);
}

void wlr_seat_pointer_notify_axis(struct wlr_seat *wlr_seat, uint32_t time,
		enum wlr_axis_orientation orientation, double value) {
	struct wlr_seat_pointer_grab *grab = wlr_seat->pointer_state.grab;
	grab->interface->axis(grab, time, orientation, value);
}

static void keyboard_switch_seat_keyboard(struct wlr_seat_handle *handle,
		struct wlr_seat_keyboard *seat_kb) {
	if (handle->seat_keyboard == seat_kb) {
		return;
	}

	// TODO: We should probably lift all of the keys set by the other
	// keyboard
	wl_keyboard_send_keymap(handle->keyboard,
		WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, seat_kb->keyboard->keymap_fd,
		seat_kb->keyboard->keymap_size);

	if (wl_resource_get_version(handle->keyboard) >=
			WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
		// TODO: Make this better
		wl_keyboard_send_repeat_info(handle->keyboard, 25, 600);
	}
	handle->seat_keyboard = seat_kb;
}

void wlr_seat_keyboard_send_key(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t key, uint32_t state) {
	struct wlr_seat_handle *handle = wlr_seat->keyboard_state.focused_handle;
	if (!handle || !handle->keyboard) {
		return;
	}

	uint32_t serial = wl_display_next_serial(wlr_seat->display);
	wl_keyboard_send_key(handle->keyboard, serial,
		time, key, state);
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard *seat_kb = wl_container_of(listener, seat_kb, key);
	struct wlr_seat *seat = seat_kb->seat;
	struct wlr_seat_handle *handle = seat->keyboard_state.focused_handle;
	if (!handle || !handle->keyboard) {
		return;
	}

	keyboard_switch_seat_keyboard(handle, seat_kb);

	struct wlr_event_keyboard_key *event = data;
	enum wlr_key_state key_state = event->state;

	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->key(grab, (uint32_t)(event->time_usec / 1000),
		event->keycode, key_state);
}

static void keyboard_modifiers_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard *seat_kb = wl_container_of(listener, seat_kb,
		modifiers);
	struct wlr_seat *seat = seat_kb->seat;
	struct wlr_seat_handle *handle = seat->keyboard_state.focused_handle;
	if (!handle || !handle->keyboard) {
		return;
	}

	keyboard_switch_seat_keyboard(handle, seat_kb);

	struct wlr_keyboard *keyboard = seat_kb->keyboard;

	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;

	grab->interface->modifiers(grab,
		keyboard->modifiers.depressed, keyboard->modifiers.latched,
		keyboard->modifiers.locked, keyboard->modifiers.group);
}

static void keyboard_keymap_notify(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard *seat_kb = wl_container_of(
			listener, seat_kb, keymap);
	wlr_log(L_DEBUG, "Keymap event for %p", seat_kb);
}

static void keyboard_destroy_notify(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard *seat_kb = wl_container_of(
			listener, seat_kb, destroy);
	wlr_seat_detach_keyboard(seat_kb->seat, seat_kb->keyboard);
}

void wlr_seat_attach_keyboard(struct wlr_seat *seat,
		struct wlr_input_device *dev) {
	assert(seat && dev && dev->type == WLR_INPUT_DEVICE_KEYBOARD);
	struct wlr_keyboard *kb = dev->keyboard;
	struct wlr_seat_keyboard *seat_kb =
		calloc(1, sizeof(struct wlr_seat_keyboard));
	seat_kb->keyboard = kb;
	seat_kb->seat = seat;
	seat_kb->key.notify = keyboard_key_notify;
	wl_signal_add(&kb->events.key, &seat_kb->key);
	seat_kb->modifiers.notify = keyboard_modifiers_notify;
	wl_signal_add(&kb->events.modifiers, &seat_kb->modifiers);
	seat_kb->keymap.notify = keyboard_keymap_notify;
	wl_signal_add(&kb->events.keymap, &seat_kb->keymap);
	// TODO: update keymap as necessary
	seat_kb->destroy.notify = keyboard_destroy_notify;
	wl_signal_add(&dev->events.destroy, &seat_kb->destroy);
	wl_list_insert(&seat->keyboards, &seat_kb->link);
}

void wlr_seat_detach_keyboard(struct wlr_seat *seat, struct wlr_keyboard *kb) {
	struct wlr_seat_keyboard *seat_kb, *_tmp;
	wl_list_for_each_safe(seat_kb, _tmp, &seat->keyboards, link) {
		if (seat_kb->keyboard == kb) {
			wl_list_remove(&seat_kb->link);
			wl_list_remove(&seat_kb->key.link);
			wl_list_remove(&seat_kb->modifiers.link);
			wl_list_remove(&seat_kb->keymap.link);
			wl_list_remove(&seat_kb->destroy.link);
			free(seat_kb);
			break;
		}
	}
}

void wlr_seat_keyboard_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_keyboard_grab *grab) {
	grab->seat = wlr_seat;
	wlr_seat->keyboard_state.grab = grab;

	wl_signal_emit(&wlr_seat->events.keyboard_grab_begin, grab);
}

void wlr_seat_keyboard_end_grab(struct wlr_seat *wlr_seat) {
	struct wlr_seat_keyboard_grab *grab = wlr_seat->keyboard_state.grab;
	wlr_seat->keyboard_state.grab = wlr_seat->keyboard_state.default_grab;

	wl_signal_emit(&wlr_seat->events.keyboard_grab_end, grab);
}

static void keyboard_surface_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state = wl_container_of(
			listener, state, surface_destroy);
	wl_list_remove(&state->surface_destroy.link);
	wl_list_init(&state->surface_destroy.link);
	state->focused_surface = NULL;
	wlr_seat_keyboard_clear_focus(state->wlr_seat);
}

static void keyboard_resource_destroy_notify(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state = wl_container_of(
			listener, state, resource_destroy);
	wl_list_remove(&state->resource_destroy.link);
	wl_list_init(&state->resource_destroy.link);
	state->focused_surface = NULL;
	wlr_seat_keyboard_clear_focus(state->wlr_seat);
}

void wlr_seat_keyboard_send_modifiers(struct wlr_seat *seat,
	uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
	uint32_t group) {
	struct wlr_seat_handle *handle = seat->keyboard_state.focused_handle;
	if (!handle || !handle->keyboard) {
		return;
	}

	uint32_t serial = wl_display_next_serial(seat->display);

	wl_keyboard_send_modifiers(handle->keyboard, serial,
		mods_depressed, mods_latched,
		mods_locked, group);
}

void wlr_seat_keyboard_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface) {
	if (wlr_seat->keyboard_state.focused_surface == surface) {
		// this surface already got an enter notify
		return;
	}

	struct wlr_seat_handle *handle = NULL;

	if (surface) {
		struct wl_client *client = wl_resource_get_client(surface->resource);
		handle = wlr_seat_handle_for_client(wlr_seat, client);
	}

	struct wlr_seat_handle *focused_handle =
		wlr_seat->keyboard_state.focused_handle;
	struct wlr_surface *focused_surface =
		wlr_seat->keyboard_state.focused_surface;

	// leave the previously entered surface
	if (focused_handle && focused_handle->keyboard && focused_surface) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_keyboard_send_leave(focused_handle->keyboard, serial,
			focused_surface->resource);
	}

	// enter the current surface
	if (handle && handle->keyboard) {
		// TODO: handle keys properly
		struct wl_array keys;
		wl_array_init(&keys);
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_keyboard_send_enter(handle->keyboard, serial,
			surface->resource, &keys);
	}

	// reinitialize the focus destroy events
	wl_list_remove(&wlr_seat->keyboard_state.surface_destroy.link);
	wl_list_init(&wlr_seat->keyboard_state.surface_destroy.link);
	wl_list_remove(&wlr_seat->keyboard_state.resource_destroy.link);
	wl_list_init(&wlr_seat->keyboard_state.resource_destroy.link);
	if (surface) {
		wl_signal_add(&surface->events.destroy,
			&wlr_seat->keyboard_state.surface_destroy);
		wl_resource_add_destroy_listener(surface->resource,
			&wlr_seat->keyboard_state.resource_destroy);
		wlr_seat->keyboard_state.resource_destroy.notify =
			keyboard_resource_destroy_notify;
		wlr_seat->keyboard_state.surface_destroy.notify =
			keyboard_surface_destroy_notify;
	}

	wlr_seat->keyboard_state.focused_handle = handle;
	wlr_seat->keyboard_state.focused_surface = surface;
}

void wlr_seat_keyboard_notify_enter(struct wlr_seat *wlr_seat, struct
		wlr_surface *surface) {
	struct wlr_seat_keyboard_grab *grab = wlr_seat->keyboard_state.grab;
	grab->interface->enter(grab, surface);
}

void wlr_seat_keyboard_clear_focus(struct wlr_seat *wlr_seat) {
	struct wl_array keys;
	wl_array_init(&keys);
	wlr_seat_keyboard_enter(wlr_seat, NULL);
}
