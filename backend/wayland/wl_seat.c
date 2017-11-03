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
	struct wlr_wl_input_device *wlr_wl_dev = (struct wlr_wl_input_device *)dev;
	assert(dev && dev->pointer);
	struct wlr_wl_pointer *wlr_wl_pointer = (struct wlr_wl_pointer *)dev->pointer;
	struct wlr_wl_backend_output *output =
		wlr_wl_output_for_surface(wlr_wl_dev->backend, surface);
	assert(output);
	wlr_wl_pointer->current_output = output;
	output->enter_serial = serial;
	wlr_wl_output_update_cursor(output);
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);
	struct wlr_wl_pointer *wlr_wl_pointer = (struct wlr_wl_pointer *)dev->pointer;
	if (wlr_wl_pointer->current_output) {
		wlr_wl_pointer->current_output->enter_serial = 0;
		wlr_wl_pointer->current_output = NULL;
	}
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);
	struct wlr_wl_pointer *wlr_wl_pointer =
		(struct wlr_wl_pointer *)dev->pointer;
	if (!wlr_wl_pointer->current_output) {
		wlr_log(L_ERROR, "pointer motion event without current output");
		return;
	}

	struct wlr_box box;
	wl_egl_window_get_attached_size(wlr_wl_pointer->current_output->egl_window,
		&box.width, &box.height);
	box.x = wl_fixed_to_int(surface_x);
	box.y = wl_fixed_to_int(surface_y);
	struct wlr_box transformed;
	wlr_output_transform_apply_to_box(
		wlr_wl_pointer->current_output->wlr_output.transform, &box,
		&transformed);

	struct wlr_event_pointer_motion_absolute wlr_event;
	wlr_event.device = dev;
	wlr_event.time_msec = time;
	wlr_event.width_mm = transformed.width;
	wlr_event.height_mm = transformed.height;
	wlr_event.x_mm = transformed.x;
	wlr_event.y_mm = transformed.y;
	wl_signal_emit(&dev->pointer->events.motion_absolute, &wlr_event);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);

	struct wlr_event_pointer_button wlr_event;
	wlr_event.device = dev;
	wlr_event.button = button;
	wlr_event.state = state;
	wlr_event.time_msec = time;
	wl_signal_emit(&dev->pointer->events.button, &wlr_event);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);
	struct wlr_wl_pointer *wlr_wl_pointer = (struct wlr_wl_pointer *)dev->pointer;

	struct wlr_event_pointer_axis wlr_event;
	wlr_event.device = dev;
	wlr_event.delta = value;
	wlr_event.orientation = axis;
	wlr_event.time_msec = time;
	wlr_event.source = wlr_wl_pointer->axis_source;
	wl_signal_emit(&dev->pointer->events.axis, &wlr_event);
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {

}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->pointer);
	struct wlr_wl_pointer *wlr_wl_pointer = (struct wlr_wl_pointer *)dev->pointer;

	wlr_wl_pointer->axis_source = axis_source;
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
	// TODO: set keymap
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
	wlr_event.device = dev;
	wlr_event.keycode = key;
	wlr_event.state = state;
	wlr_event.time_msec = time;
	wlr_keyboard_notify_key(dev->keyboard, &wlr_event);
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->keyboard);
	struct wlr_event_keyboard_modifiers wlr_event;
	wlr_event.device = dev;
	wlr_event.keyboard = dev->keyboard;
	wlr_event.mods_depressed = mods_depressed;
	wlr_event.mods_latched = mods_latched;
	wlr_event.mods_locked = mods_locked;
	wlr_event.group = group;

	wlr_keyboard_notify_modifiers(dev->keyboard, &wlr_event);
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

static void input_device_destroy(struct wlr_input_device *_dev) {
	struct wlr_wl_input_device *dev = (struct wlr_wl_input_device *)_dev;
	wl_signal_emit(&dev->backend->backend.events.input_remove, &dev->wlr_input_device);
	if (dev->resource) {
		wl_proxy_destroy(dev->resource);
	}
	free(dev);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = input_device_destroy
};

static struct wlr_input_device *allocate_device(struct wlr_wl_backend *backend,
		enum wlr_input_device_type type) {
	struct wlr_wl_input_device *wlr_wl_dev;
	if (!(wlr_wl_dev = calloc(1, sizeof(struct wlr_wl_input_device)))) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_wl_dev->backend = backend;

	int vendor = 0;
	int product = 0;
	const char *name = "wayland";
	struct wlr_input_device *wlr_device = &wlr_wl_dev->wlr_input_device;
	wlr_input_device_init(wlr_device, type, &input_device_impl,
			name, vendor, product);
	wl_list_insert(&backend->devices, &wlr_device->link);
	return wlr_device;
}

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct wlr_wl_backend *backend = data;
	assert(backend->seat == wl_seat);

	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		wlr_log(L_DEBUG, "seat %p offered pointer", (void*) wl_seat);
		struct wlr_wl_pointer *wlr_wl_pointer;
		if (!(wlr_wl_pointer = calloc(1, sizeof(struct wlr_wl_pointer)))) {
			wlr_log(L_ERROR, "Unable to allocate wlr_wl_pointer");
			return;
		}

		struct wlr_input_device *wlr_device;
		if (!(wlr_device = allocate_device(backend, WLR_INPUT_DEVICE_POINTER))) {
			free(wlr_wl_pointer);
			wlr_log(L_ERROR, "Unable to allocate wlr_device for pointer");
			return;
		}
		struct wlr_wl_input_device *wlr_wl_device =
			(struct wlr_wl_input_device *)wlr_device;

		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(wl_pointer, &pointer_listener, wlr_device);
		wlr_device->pointer = &wlr_wl_pointer->wlr_pointer;
		wlr_pointer_init(wlr_device->pointer, NULL);
		wlr_wl_device->resource = wl_pointer;
		wl_signal_emit(&backend->backend.events.input_add, wlr_device);
		backend->pointer = wl_pointer;
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		wlr_log(L_DEBUG, "seat %p offered keyboard", (void*) wl_seat);
		struct wlr_input_device *wlr_device = allocate_device(backend,
			WLR_INPUT_DEVICE_KEYBOARD);
		if (!wlr_device) {
			wlr_log(L_ERROR, "Unable to allocate wl_keyboard device");
			return;
		}
		wlr_device->keyboard = calloc(1, sizeof(struct wlr_keyboard));
		if (!wlr_device->keyboard) {
			free(wlr_device);
			wlr_log(L_ERROR, "Unable to allocate wlr keyboard");
			return;
		}
		wlr_keyboard_init(wlr_device->keyboard, NULL);
		struct wlr_wl_input_device *wlr_wl_device =
			(struct wlr_wl_input_device *)wlr_device;

		struct wl_keyboard *wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(wl_keyboard, &keyboard_listener, wlr_device);
		wlr_wl_device->resource = wl_keyboard;
		wl_signal_emit(&backend->backend.events.input_add, wlr_device);
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
	struct wlr_wl_backend *backend = data;
	assert(backend->seat == wl_seat);
	// Do we need to check if seatName was previously set for name change?
	free(backend->seat_name);
	backend->seat_name = strdup(name);
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};
