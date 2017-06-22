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
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x,
		wl_fixed_t surface_y) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer && dev->pointer->state);
	struct wlr_output* output = wlr_wl_output_for_surface(dev->state->backend,
			surface);

	if (!output) {
		wlr_log(L_ERROR, "pointer entered invalid surface");
		return;
	}

	dev->pointer->state->current_output = output;
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer && dev->pointer->state);
	dev->pointer->state->current_output = NULL;
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer && dev->pointer->state);
	struct wlr_pointer_state *state = dev->pointer->state;

	if(!state->current_output) {
		wlr_log(L_ERROR, "pointer motion event without current output");
		return;
	}

	int width, height;
	wl_egl_window_get_attached_size(state->current_output->state->egl_window,
		&width, &height);

	struct wlr_event_pointer_motion_absolute wlr_event;
	wlr_event.time_sec = time / 1000;
	wlr_event.time_usec = time * 1000;
	wlr_event.width_mm = width;
	wlr_event.height_mm = height;
	wlr_event.x_mm = wl_fixed_to_double(surface_x);
	wlr_event.y_mm = wl_fixed_to_double(surface_y);
	wl_signal_emit(&dev->pointer->events.motion_absolute, &wlr_event);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);

	struct wlr_event_pointer_button wlr_event;
	wlr_event.button = button;
	wlr_event.state = state;
	wlr_event.time_sec = time / 1000;
	wlr_event.time_usec = time * 1000;
	wl_signal_emit(&dev->pointer->events.button, &wlr_event);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);

	struct wlr_event_pointer_axis wlr_event;
	wlr_event.delta = value;
	wlr_event.orientation = axis;
	wlr_event.time_sec = time / 1000;
	wlr_event.time_usec = time * 1000;
	wlr_event.source = dev->pointer->state->axis_source;
	wl_signal_emit(&dev->pointer->events.axis, &wlr_event);
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {

}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer && dev->pointer->state);
	dev->pointer->state->axis_source = axis_source;
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
	struct wlr_input_device *dev = data;
	assert(dev && dev->keyboard);

	struct wlr_event_keyboard_key wlr_event;
	wlr_event.keycode = key;
	wlr_event.state = state;
	wlr_event.time_sec = time / 1000;
	wlr_event.time_usec = time * 1000;
	wl_signal_emit(&dev->keyboard->events.key, &wlr_event);
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
	if (state->resource)
		wl_proxy_destroy(state->resource);
	free(state);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = input_device_destroy
};

static void pointer_destroy(struct wlr_pointer_state *state) {
	free(state);
}

static struct wlr_pointer_impl pointer_impl = {
	.destroy = pointer_destroy
};

static struct wlr_input_device *allocate_device(struct wlr_backend_state *state,
		enum wlr_input_device_type type) {
	struct wlr_input_device_state *devstate;
	if (!(devstate = calloc(1, sizeof(struct wlr_input_device_state)))) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	devstate->backend = state;

	int vendor = 0;
	int product = 0;
	const char *name = "wayland";
	struct wlr_input_device *wlr_device = wlr_input_device_create(
		type, &input_device_impl, devstate,
		name, vendor, product);
	if (!wlr_device) {
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
		wlr_log(L_DEBUG, "seat %p offered pointer", (void*) wl_seat);
		struct wlr_pointer_state *pointer_state;
		if (!(pointer_state = calloc(1, sizeof(struct wlr_pointer_state)))) {
			wlr_log(L_ERROR, "Unable to allocate wlr_pointer_state");
			return;
		}

		struct wlr_input_device *wlr_device;
		if (!(wlr_device = allocate_device(state, WLR_INPUT_DEVICE_POINTER))) {
			free(pointer_state);
			wlr_log(L_ERROR, "Unable to allocate wlr_device for pointer");
			return;
		}

		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(wl_pointer, &pointer_listener, wlr_device);
		wlr_device->pointer = wlr_pointer_create(&pointer_impl, pointer_state);
		wlr_device->state->resource = wl_pointer;
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		wlr_log(L_DEBUG, "seat %p offered keyboard", (void*) wl_seat);
		struct wlr_input_device *wlr_device = allocate_device(state,
			WLR_INPUT_DEVICE_KEYBOARD);
		if (!wlr_device) {
			wlr_log(L_ERROR, "Unable to allocate wl_pointer device");
			return;
		}

		struct wl_keyboard *wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(wl_keyboard, &keyboard_listener, wlr_device);
		wlr_device->keyboard = wlr_keyboard_create(NULL, NULL);
		wlr_device->state->resource = wl_keyboard;
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
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
