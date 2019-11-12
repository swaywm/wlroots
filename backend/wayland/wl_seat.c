#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>

#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/util/log.h>

#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "backend/wayland.h"
#include "util/signal.h"

static struct wlr_wl_pointer *output_get_pointer(struct wlr_wl_output *output) {
	struct wlr_input_device *wlr_dev;
	wl_list_for_each(wlr_dev, &output->backend->devices, link) {
		if (wlr_dev->type != WLR_INPUT_DEVICE_POINTER) {
			continue;
		}
		struct wlr_wl_pointer *pointer = pointer_get_wl(wlr_dev->pointer);
		if (pointer->output == output) {
			return pointer;
		}
	}

	return NULL;
}

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
		wl_fixed_t sy) {
	struct wlr_wl_backend *backend = data;
	if (surface == NULL) {
		return;
	}

	struct wlr_wl_output *output = wl_surface_get_user_data(surface);
	assert(output);
	struct wlr_wl_pointer *pointer = output_get_pointer(output);

	output->enter_serial = serial;
	backend->current_pointer = pointer;
	update_wl_output_cursor(output);
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct wlr_wl_backend *backend = data;
	if (surface == NULL) {
		return;
	}

	struct wlr_wl_output *output = wl_surface_get_user_data(surface);
	assert(output);
	output->enter_serial = 0;

	if (backend->current_pointer == NULL ||
			backend->current_pointer->output != output) {
		return;
	}

	backend->current_pointer = NULL;
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	struct wlr_output *wlr_output = &pointer->output->wlr_output;
	struct wlr_event_pointer_motion_absolute event = {
		.device = &pointer->input_device->wlr_input_device,
		.time_msec = time,
		.x = wl_fixed_to_double(sx) / wlr_output->width,
		.y = wl_fixed_to_double(sy) / wlr_output->height,
	};
	wlr_signal_emit_safe(&pointer->wlr_pointer.events.motion_absolute, &event);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	struct wlr_event_pointer_button event = {
		.device = &pointer->input_device->wlr_input_device,
		.button = button,
		.state = state,
		.time_msec = time,
	};
	wlr_signal_emit_safe(&pointer->wlr_pointer.events.button, &event);
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	struct wlr_event_pointer_axis event = {
		.device = &pointer->input_device->wlr_input_device,
		.delta = wl_fixed_to_double(value),
		.delta_discrete = pointer->axis_discrete,
		.orientation = axis,
		.time_msec = time,
		.source = pointer->axis_source,
	};
	wlr_signal_emit_safe(&pointer->wlr_pointer.events.axis, &event);

	pointer->axis_discrete = 0;
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	wlr_signal_emit_safe(&pointer->wlr_pointer.events.frame,
		&pointer->wlr_pointer);
}

static void pointer_handle_axis_source(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis_source) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	pointer->axis_source = axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	struct wlr_event_pointer_axis event = {
		.device = &pointer->input_device->wlr_input_device,
		.delta = 0,
		.delta_discrete = 0,
		.orientation = axis,
		.time_msec = time,
		.source = pointer->axis_source,
	};
	wlr_signal_emit_safe(&pointer->wlr_pointer.events.axis, &event);
}

static void pointer_handle_axis_discrete(void *data,
		struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
	struct wlr_wl_backend *backend = data;
	struct wlr_wl_pointer *pointer = backend->current_pointer;
	if (pointer == NULL) {
		return;
	}

	pointer->axis_discrete = discrete;
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
	.axis_discrete = pointer_handle_axis_discrete,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	// TODO: set keymap
}

static uint32_t get_current_time_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_nsec / 1000;
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	struct wlr_input_device *dev = data;

	uint32_t time = get_current_time_msec();

	uint32_t *keycode_ptr;
	wl_array_for_each(keycode_ptr, keys) {
		struct wlr_event_keyboard_key event = {
			.keycode = *keycode_ptr,
			.state = WLR_KEY_PRESSED,
			.time_msec = time,
			.update_state = false,
		};
		wlr_keyboard_notify_key(dev->keyboard, &event);
	}
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	struct wlr_input_device *dev = data;

	uint32_t time = get_current_time_msec();

	uint32_t pressed[dev->keyboard->num_keycodes + 1];
	memcpy(pressed, dev->keyboard->keycodes,
		dev->keyboard->num_keycodes * sizeof(uint32_t));

	for (size_t i = 0; i < sizeof(pressed)/sizeof(pressed[0]); ++i) {
		uint32_t keycode = pressed[i];

		struct wlr_event_keyboard_key event = {
			.keycode = keycode,
			.state = WLR_KEY_RELEASED,
			.time_msec = time,
			.update_state = false,
		};
		wlr_keyboard_notify_key(dev->keyboard, &event);
	}
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->keyboard);

	struct wlr_event_keyboard_key wlr_event = {
		.keycode = key,
		.state = state,
		.time_msec = time,
		.update_state = false,
	};
	wlr_keyboard_notify_key(dev->keyboard, &wlr_event);
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	struct wlr_input_device *dev = data;
	assert(dev && dev->keyboard);
	wlr_keyboard_notify_modifiers(dev->keyboard, mods_depressed, mods_latched,
		mods_locked, group);
}

static void keyboard_handle_repeat_info(void *data,
		struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
	// This space is intentionally left blank
}

static struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info
};

static struct wlr_wl_input_device *get_wl_input_device_from_input_device(
		struct wlr_input_device *wlr_dev) {
	assert(wlr_input_device_is_wl(wlr_dev));
	return (struct wlr_wl_input_device *)wlr_dev;
}

static void input_device_destroy(struct wlr_input_device *wlr_dev) {
	struct wlr_wl_input_device *dev =
		get_wl_input_device_from_input_device(wlr_dev);
	if (dev->resource) {
		wl_proxy_destroy(dev->resource);
	}
	wl_list_remove(&dev->wlr_input_device.link);
	free(dev);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = input_device_destroy,
};

bool wlr_input_device_is_wl(struct wlr_input_device *dev) {
	return dev->impl == &input_device_impl;
}

struct wlr_wl_input_device *create_wl_input_device(
		struct wlr_wl_backend *backend, enum wlr_input_device_type type) {
	struct wlr_wl_input_device *dev =
		calloc(1, sizeof(struct wlr_wl_input_device));
	if (dev == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	dev->backend = backend;

	struct wlr_input_device *wlr_dev = &dev->wlr_input_device;

	unsigned int vendor = 0, product = 0;
	const char *name = "wayland";
	wlr_input_device_init(wlr_dev, type, &input_device_impl, name, vendor,
		product);
	wl_list_insert(&backend->devices, &wlr_dev->link);
	return dev;
}

static struct wlr_pointer_impl pointer_impl;

struct wlr_wl_pointer *pointer_get_wl(struct wlr_pointer *wlr_pointer) {
	assert(wlr_pointer->impl == &pointer_impl);
	return (struct wlr_wl_pointer *)wlr_pointer;
}

static void pointer_destroy(struct wlr_pointer *wlr_pointer) {
	struct wlr_wl_pointer *pointer = pointer_get_wl(wlr_pointer);

	if (pointer->output->backend->current_pointer == pointer) {
		pointer->output->backend->current_pointer = NULL;
	}

	wl_list_remove(&pointer->output_destroy.link);
	free(pointer);
}

static struct wlr_pointer_impl pointer_impl = {
	.destroy = pointer_destroy,
};

static void gesture_swipe_begin(void *data,
		struct zwp_pointer_gesture_swipe_v1 *zwp_pointer_gesture_swipe_v1,
		uint32_t serial, uint32_t time,
		struct wl_surface *surface, uint32_t fingers) {
	struct wlr_wl_input_device *input_device = (struct wlr_wl_input_device *)data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;
	struct wlr_event_pointer_swipe_begin wlr_event = {
		.device = wlr_dev,
		.time_msec = time,
		.fingers = fingers,
	};
	input_device->fingers = fingers;
	wlr_signal_emit_safe(&wlr_dev->pointer->events.swipe_begin, &wlr_event);
}

static void gesture_swipe_update(void *data,
		struct zwp_pointer_gesture_swipe_v1 *zwp_pointer_gesture_swipe_v1,
		uint32_t time, wl_fixed_t dx, wl_fixed_t dy) {
	struct wlr_wl_input_device *input_device = (struct wlr_wl_input_device *)data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;
	struct wlr_event_pointer_swipe_update wlr_event = {
		.device = wlr_dev,
		.time_msec = time,
		.fingers = input_device->fingers,
		.dx = wl_fixed_to_double(dx),
		.dy = wl_fixed_to_double(dy),
	};
	wlr_signal_emit_safe(&wlr_dev->pointer->events.swipe_update, &wlr_event);
}

static void gesture_swipe_end(void *data,
		struct zwp_pointer_gesture_swipe_v1 *zwp_pointer_gesture_swipe_v1,
		uint32_t serial, uint32_t time, int32_t cancelled) {
	struct wlr_wl_input_device *input_device = (struct wlr_wl_input_device *)data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;
	struct wlr_event_pointer_swipe_end wlr_event = {
		.device = wlr_dev,
		.time_msec = time,
		.cancelled = cancelled,
	};
	wlr_signal_emit_safe(&wlr_dev->pointer->events.swipe_end, &wlr_event);
}

static struct zwp_pointer_gesture_swipe_v1_listener gesture_swipe_impl = {
	.begin = gesture_swipe_begin,
	.update = gesture_swipe_update,
	.end = gesture_swipe_end,
};

static void gesture_pinch_begin(void *data,
		struct zwp_pointer_gesture_pinch_v1 *zwp_pointer_gesture_pinch_v1,
		uint32_t serial, uint32_t time,
		struct wl_surface *surface, uint32_t fingers) {
	struct wlr_wl_input_device *input_device = (struct wlr_wl_input_device *)data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;
	struct wlr_event_pointer_pinch_begin wlr_event = {
		.device = wlr_dev,
		.time_msec = time,
		.fingers = fingers,
	};
	input_device->fingers = fingers;
	wlr_signal_emit_safe(&wlr_dev->pointer->events.pinch_begin, &wlr_event);
}

static void gesture_pinch_update(void *data,
		struct zwp_pointer_gesture_pinch_v1 *zwp_pointer_gesture_pinch_v1,
		uint32_t time, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale, wl_fixed_t rotation) {
	struct wlr_wl_input_device *input_device = (struct wlr_wl_input_device *)data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;
	struct wlr_event_pointer_pinch_update wlr_event = {
		.device = wlr_dev,
		.time_msec = time,
		.fingers = input_device->fingers,
		.dx = wl_fixed_to_double(dx),
		.dy = wl_fixed_to_double(dy),
		.scale = wl_fixed_to_double(scale),
		.rotation = wl_fixed_to_double(rotation),
	};
	wlr_signal_emit_safe(&wlr_dev->pointer->events.pinch_update, &wlr_event);
}

static void gesture_pinch_end(void *data,
		struct zwp_pointer_gesture_pinch_v1 *zwp_pointer_gesture_pinch_v1,
		uint32_t serial, uint32_t time, int32_t cancelled) {
	struct wlr_wl_input_device *input_device = (struct wlr_wl_input_device *)data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;
	struct wlr_event_pointer_pinch_end wlr_event = {
		.device = wlr_dev,
		.time_msec = time,
		.cancelled = cancelled,
	};
	wlr_signal_emit_safe(&wlr_dev->pointer->events.pinch_end, &wlr_event);
}

static struct zwp_pointer_gesture_pinch_v1_listener gesture_pinch_impl = {
	.begin = gesture_pinch_begin,
	.update = gesture_pinch_update,
	.end = gesture_pinch_end,
};


void relative_pointer_handle_relative_motion(void *data,
		struct zwp_relative_pointer_v1 *relative_pointer, uint32_t utime_hi,
		uint32_t utime_lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_unaccel,
		wl_fixed_t dy_unaccel) {
	struct wlr_wl_input_device *input_device = data;
	struct wlr_input_device *wlr_dev = &input_device->wlr_input_device;

	uint64_t time_usec = (uint64_t)utime_hi << 32 | utime_lo;

	struct wlr_event_pointer_motion wlr_event = {
		.device = wlr_dev,
		.time_msec = (uint32_t)(time_usec / 1000),
		.delta_x = wl_fixed_to_double(dx),
		.delta_y = wl_fixed_to_double(dy),
		.unaccel_dx = wl_fixed_to_double(dx_unaccel),
		.unaccel_dy = wl_fixed_to_double(dy_unaccel),
	};
	wlr_signal_emit_safe(&wlr_dev->pointer->events.motion, &wlr_event);
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
	.relative_motion = relative_pointer_handle_relative_motion,
};


static void pointer_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_wl_pointer *pointer =
		wl_container_of(listener, pointer, output_destroy);
	wlr_input_device_destroy(&pointer->input_device->wlr_input_device);
}

void create_wl_pointer(struct wl_pointer *wl_pointer, struct wlr_wl_output *output) {
	struct wlr_wl_backend *backend = output->backend;

	struct wlr_input_device *wlr_dev;
	wl_list_for_each(wlr_dev, &output->backend->devices, link) {
		if (wlr_dev->type != WLR_INPUT_DEVICE_POINTER) {
			continue;
		}
		struct wlr_wl_pointer *pointer = pointer_get_wl(wlr_dev->pointer);
		if (pointer->output == output) {
			return;
		}
	}

	struct wlr_wl_pointer *pointer = calloc(1, sizeof(struct wlr_wl_pointer));
	if (pointer == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return;
	}
	pointer->wl_pointer = wl_pointer;
	pointer->output = output;

	wl_signal_add(&output->wlr_output.events.destroy, &pointer->output_destroy);
	pointer->output_destroy.notify = pointer_handle_output_destroy;

	struct wlr_wl_input_device *dev =
		create_wl_input_device(backend, WLR_INPUT_DEVICE_POINTER);
	if (dev == NULL) {
		free(pointer);
		wlr_log(WLR_ERROR, "Allocation failed");
		return;
	}
	pointer->input_device = dev;

	wlr_dev = &dev->wlr_input_device;
	wlr_dev->pointer = &pointer->wlr_pointer;
	wlr_dev->output_name = strdup(output->wlr_output.name);
	wlr_pointer_init(wlr_dev->pointer, &pointer_impl);

	if (backend->zwp_pointer_gestures_v1) {
		pointer->gesture_swipe = zwp_pointer_gestures_v1_get_swipe_gesture(
				backend->zwp_pointer_gestures_v1, wl_pointer);
		zwp_pointer_gesture_swipe_v1_add_listener(pointer->gesture_swipe, &gesture_swipe_impl, dev);
		pointer->gesture_pinch = zwp_pointer_gestures_v1_get_pinch_gesture(
				backend->zwp_pointer_gestures_v1, wl_pointer);
		zwp_pointer_gesture_pinch_v1_add_listener(pointer->gesture_pinch, &gesture_pinch_impl, dev);
	}

	if (backend->zwp_relative_pointer_manager_v1) {
		pointer->relative_pointer =
			zwp_relative_pointer_manager_v1_get_relative_pointer(
			backend->zwp_relative_pointer_manager_v1, wl_pointer);
		zwp_relative_pointer_v1_add_listener(pointer->relative_pointer,
			&relative_pointer_listener, dev);
	}

	wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
}

void create_wl_keyboard(struct wl_keyboard *wl_keyboard, struct wlr_wl_backend *wl) {
	struct wlr_wl_input_device *dev =
		create_wl_input_device(wl, WLR_INPUT_DEVICE_KEYBOARD);
	if (!dev) {
		return;
	}

	struct wlr_input_device *wlr_dev = &dev->wlr_input_device;

	wlr_dev->keyboard = calloc(1, sizeof(*wlr_dev->keyboard));
	if (!wlr_dev->keyboard) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(dev);
		return;
	}
	wlr_keyboard_init(wlr_dev->keyboard, NULL);

	wl_keyboard_add_listener(wl_keyboard, &keyboard_listener, wlr_dev);
	dev->resource = wl_keyboard;
	wlr_signal_emit_safe(&wl->backend.events.new_input, wlr_dev);
}

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct wlr_wl_backend *backend = data;
	assert(backend->seat == wl_seat);

	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		wlr_log(WLR_DEBUG, "seat %p offered pointer", (void*) wl_seat);

		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);
		backend->pointer = wl_pointer;

		struct wlr_wl_output *output;
		wl_list_for_each(output, &backend->outputs, link) {
			create_wl_pointer(wl_pointer, output);
		}

		wl_pointer_add_listener(wl_pointer, &pointer_listener, backend);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		wlr_log(WLR_DEBUG, "seat %p offered keyboard", (void*) wl_seat);

		struct wl_keyboard *wl_keyboard = wl_seat_get_keyboard(wl_seat);
		backend->keyboard = wl_keyboard;

		if (backend->started) {
			create_wl_keyboard(wl_keyboard, backend);
		}
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {
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
