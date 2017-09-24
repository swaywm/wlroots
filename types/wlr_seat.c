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

static void wl_pointer_set_cursor(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t serial,
		struct wl_resource *surface,
		int32_t hotspot_x,
		int32_t hotspot_y) {
	wlr_log(L_DEBUG, "TODO: wl_pointer_set_cursor");
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
		// code and it'd be stupid for clients to create several pointers for
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
	wl_seat_send_name(handle->wl_resource, wlr_seat->name);
	wl_seat_send_capabilities(handle->wl_resource, wlr_seat->capabilities);
	wl_signal_emit(&wlr_seat->events.client_bound, handle);
}

struct wlr_seat *wlr_seat_create(struct wl_display *display, const char *name) {
	struct wlr_seat *wlr_seat = calloc(1, sizeof(struct wlr_seat));
	if (!wlr_seat) {
		return NULL;
	}

	wlr_seat->pointer_state.wlr_seat = wlr_seat;

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

	wl_list_init(&wlr_seat->pointer_state.focus_resource_destroy_listener.link);
	wl_list_init(&wlr_seat->pointer_state.focus_surface_destroy_listener.link);

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

static void handle_pointer_focus_surface_destroyed(
		struct wl_listener *listener, void *data) {
	struct wlr_seat_pointer_state *state =
		wl_container_of(listener, state, focus_surface_destroy_listener);

	state->focused_surface = NULL;
	wlr_seat_pointer_clear_focus(state->wlr_seat);
}

static void handle_pointer_focus_resource_destroyed(
		struct wl_listener *listener, void *data) {
	struct wlr_seat_pointer_state *state =
		wl_container_of(listener, state, focus_resource_destroy_listener);

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
		wl_pointer_send_frame(focused_handle->pointer);
	}

	// TEMPORARY
	if (focused_handle && focused_handle->pointer && focused_surface) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_keyboard_send_leave(focused_handle->keyboard,
				serial, focused_surface->resource);
	}

	// enter the current surface
	if (handle && handle->pointer) {
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_pointer_send_enter(handle->pointer, serial, surface->resource,
			wl_fixed_from_double(sx), wl_fixed_from_double(sy));
		wl_pointer_send_frame(handle->pointer);
	}

	// TEMPORARY
	if (handle && handle->keyboard) {
		wlr_log(L_DEBUG, "Sending keyboard");
		struct wl_array keys;
		wl_array_init(&keys);
		uint32_t serial = wl_display_next_serial(wlr_seat->display);
		wl_keyboard_send_enter(handle->keyboard, serial, surface->resource, &keys);
	}

	// reinitialize the focus destroy events
	wl_list_remove(
		&wlr_seat->pointer_state.focus_surface_destroy_listener.link);
	wl_list_init(&wlr_seat->pointer_state.focus_surface_destroy_listener.link);
	wl_list_remove(
		&wlr_seat->pointer_state.focus_resource_destroy_listener.link);
	wl_list_init(&wlr_seat->pointer_state.focus_resource_destroy_listener.link);
	if (surface) {
		wl_signal_add(&surface->signals.destroy,
			&wlr_seat->pointer_state.focus_surface_destroy_listener);
		wl_resource_add_destroy_listener(surface->resource,
			&wlr_seat->pointer_state.focus_resource_destroy_listener);
		wlr_seat->pointer_state.focus_resource_destroy_listener.notify =
			handle_pointer_focus_resource_destroyed;
		wlr_seat->pointer_state.focus_surface_destroy_listener.notify =
			handle_pointer_focus_surface_destroyed;
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
	wl_pointer_send_frame(wlr_seat->pointer_state.focused_handle->pointer);
}

uint32_t wlr_seat_pointer_send_button(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t button, uint32_t state) {
	if (!wlr_seat_pointer_has_focus_resource(wlr_seat)) {
		return 0;
	}

	uint32_t serial = wl_display_next_serial(wlr_seat->display);
	wl_pointer_send_button(wlr_seat->pointer_state.focused_handle->pointer,
		serial, time, button, state);
	wl_pointer_send_frame(wlr_seat->pointer_state.focused_handle->pointer);
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
	} else {
		wl_pointer_send_axis_stop(pointer, time, orientation);
	}

	wl_pointer_send_frame(pointer);
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard *seat_kb = wl_container_of(
			listener, seat_kb, key);
	struct wlr_seat *seat = seat_kb->seat;
	struct wlr_seat_handle *handle = seat->pointer_state.focused_handle;
	if (!handle || !handle->keyboard) {
		return;
	}
	struct wlr_keyboard *keyboard = seat_kb->keyboard;
	struct wlr_event_keyboard_key *event = data;
	enum wlr_key_state key_state = event->state;
	if (handle->seat_keyboard != seat_kb) {
		// TODO: We should probably lift all of the keys set by the other
		// keyboard
		wlr_log(L_DEBUG, "Sending key map");
		wl_keyboard_send_keymap(handle->keyboard,
				WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
				seat_kb->keyboard->keymap_fd,
				seat_kb->keyboard->keymap_size);

		if (wl_resource_get_version(handle->keyboard) >= 2) {
			// TODO: Make this better
			wl_keyboard_send_repeat_info(handle->keyboard, 25, 600);
		}
		handle->seat_keyboard = seat_kb;
	}

	uint32_t depressed = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_DEPRESSED);
	uint32_t latched = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_LATCHED);
	uint32_t locked = xkb_state_serialize_mods(keyboard->xkb_state,
		XKB_STATE_MODS_LOCKED);
	uint32_t group = xkb_state_serialize_layout(keyboard->xkb_state,
		XKB_STATE_LAYOUT_EFFECTIVE);

	uint32_t modifiers_serial = wl_display_next_serial(seat->display);
	uint32_t key_serial = wl_display_next_serial(seat->display);
	wl_keyboard_send_modifiers(handle->keyboard, modifiers_serial,
		depressed, latched, locked, group);
	wl_keyboard_send_key(handle->keyboard, key_serial,
			(uint32_t)event->time_usec, event->keycode, key_state);
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
	wl_list_init(&seat_kb->key.link);
	seat_kb->key.notify = keyboard_key_notify;
	wl_signal_add(&kb->events.key, &seat_kb->key);
	wl_list_init(&seat_kb->keymap.link);
	seat_kb->keymap.notify = keyboard_keymap_notify;
	wl_signal_add(&kb->events.keymap, &seat_kb->keymap);
	wl_list_init(&seat_kb->destroy.link);
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
			wl_list_remove(&seat_kb->keymap.link);
			wl_list_remove(&seat_kb->destroy.link);
			free(seat_kb);
			break;
		}
	}
}
