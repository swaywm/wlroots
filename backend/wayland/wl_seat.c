#define _XOPEN_SOURCE 500
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x,
		wl_fixed_t surface_y) {

}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {

}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {

}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {

}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {

}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {

}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source) {

}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis) {

}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete) {

}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
	.axis_source = pointer_handle_axis_source,
	.axis_stop = pointer_handle_axis_stop,
	.axis_discrete = pointer_handle_axis_discrete
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {

}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {

}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {

}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {

}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {

}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
	int32_t rate, int32_t delay) {

}

static struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info
};

static void input_device_destroy(struct wlr_input_device_state *state) {
	free(state);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = input_device_destroy
};

static struct wlr_input_device *allocate_device(struct wlr_backend_state *state,
		enum wlr_input_device_type type) {
	struct wlr_input_device_state *devstate =
		calloc(1, sizeof(struct wlr_input_device_state));
	if(!devstate) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	// TODO: any way to retrieve those information?
	int vendor = 0;
	int product = 0;
	const char *name = "unknown;wayland";
	struct wlr_input_device *wlr_device = wlr_input_device_create(
		type, &input_device_impl, devstate,
		name, vendor, product);
	if(!wlr_device) {
		free(devstate);
		return NULL;
	}

	list_add(state->devices, wlr_device);
	return wlr_device;
}

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct wlr_backend_state *state = data;
	assert(state->seat == wl_seat);

	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		wlr_log(L_DEBUG, "seat %p offered pointer", wl_seat);
		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);

		struct wlr_input_device *wlr_device = allocate_device(state,
			WLR_INPUT_DEVICE_POINTER);
		if(!wlr_device) {
			wl_pointer_destroy(wl_pointer);
			wlr_log(L_ERROR, "Unable to allocate wl_pointer device");
			return;
		}

		wlr_device->pointer = wlr_pointer_create(NULL, NULL);
		list_add(state->devices, wlr_device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		wlr_log(L_DEBUG, "seat %p offered keyboard", wl_seat);
		struct wl_keyboard *wl_keyboard = wl_seat_get_keyboard(wl_seat);
		struct wlr_input_device *wlr_device = allocate_device(state,
			WLR_INPUT_DEVICE_KEYBOARD);
		if(!wlr_device) {
			wl_keyboard_release(wl_keyboard);
			wlr_log(L_ERROR, "Unable to allocate wl_pointer device");
			return;
		}

		wlr_device->keyboard = wlr_keyboard_create(NULL, NULL);
		list_add(state->devices, wlr_device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}

	// TODO: touch
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
	struct wlr_backend_state *state = data;
	assert(state->seat == wl_seat);
	state->seatName = strdup(name);
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};
