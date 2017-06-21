#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"

struct wlr_pointer *wlr_libinput_pointer_create(
		struct libinput_device *device) {
	assert(device);
	return wlr_pointer_create(NULL, NULL);
}

void handle_pointer_motion(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_motion *wlr_event =
		calloc(1, sizeof(struct wlr_event_pointer_motion));
	wlr_event->time_sec = libinput_event_pointer_get_time(pevent);
	wlr_event->time_usec = libinput_event_pointer_get_time_usec(pevent);
	wlr_event->delta_x = libinput_event_pointer_get_dx(pevent);
	wlr_event->delta_y = libinput_event_pointer_get_dy(pevent);
	wl_signal_emit(&dev->pointer->events.motion, wlr_event);
}

void handle_pointer_motion_abs(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_motion_absolute *wlr_event =
		calloc(1, sizeof(struct wlr_event_pointer_motion_absolute));
	wlr_event->time_sec = libinput_event_pointer_get_time(pevent);
	wlr_event->time_usec = libinput_event_pointer_get_time_usec(pevent);
	wlr_event->x_mm = libinput_event_pointer_get_absolute_x(pevent);
	wlr_event->y_mm = libinput_event_pointer_get_absolute_y(pevent);
	libinput_device_get_size(device, &wlr_event->width_mm, &wlr_event->height_mm);
	wl_signal_emit(&dev->pointer->events.motion_absolute, wlr_event);
}

void handle_pointer_button(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_button *wlr_event =
		calloc(1, sizeof(struct wlr_event_pointer_button));
	wlr_event->time_sec = libinput_event_pointer_get_time(pevent);
	wlr_event->time_usec = libinput_event_pointer_get_time_usec(pevent);
	wlr_event->button = libinput_event_pointer_get_button(pevent);
	switch (libinput_event_pointer_get_button_state(pevent)) {
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event->state = WLR_BUTTON_PRESSED;
		break;
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event->state = WLR_BUTTON_RELEASED;
		break;
	}
	wl_signal_emit(&dev->pointer->events.button, wlr_event);
}

void handle_pointer_axis(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_axis *wlr_event =
		calloc(1, sizeof(struct wlr_event_pointer_axis));
	wlr_event->time_sec = libinput_event_pointer_get_time(pevent);
	wlr_event->time_usec = libinput_event_pointer_get_time_usec(pevent);
	switch (libinput_event_pointer_get_axis_source(pevent)) {
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
		wlr_event->source = WLR_AXIS_SOURCE_WHEEL;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
		wlr_event->source = WLR_AXIS_SOURCE_FINGER;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
		wlr_event->source = WLR_AXIS_SOURCE_CONTINUOUS;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
		wlr_event->source = WLR_AXIS_SOURCE_WHEEL_TILT;
		break;
	}
	enum libinput_pointer_axis axies[] = {
		LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL,
		LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL,
	};
	for (size_t i = 0; i < sizeof(axies) / sizeof(axies[0]); ++i) {
		if (libinput_event_pointer_has_axis(pevent, axies[i])) {
			switch (axies[i]) {
			case LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL:
				wlr_event->orientation = WLR_AXIS_ORIENTATION_VERTICAL;
				break;
			case LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL:
				wlr_event->orientation = WLR_AXIS_ORIENTATION_HORIZONTAL;
				break;
			}
			wlr_event->delta = libinput_event_pointer_get_axis_value(
					pevent, axies[i]);
		}
		wl_signal_emit(&dev->pointer->events.axis, wlr_event);
	}
}
