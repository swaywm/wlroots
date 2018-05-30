#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "util/signal.h"

struct wlr_pointer *create_libinput_pointer(
		struct libinput_device *libinput_dev) {
	assert(libinput_dev);
	struct wlr_pointer *wlr_pointer = calloc(1, sizeof(struct wlr_pointer));
	if (!wlr_pointer) {
		wlr_log(WLR_ERROR, "Unable to allocate wlr_pointer");
		return NULL;
	}
	wlr_pointer_init(wlr_pointer, NULL);
	return wlr_pointer;
}

void handle_pointer_motion(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_motion wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_pointer_get_time_usec(pevent));
	wlr_event.delta_x = libinput_event_pointer_get_dx(pevent);
	wlr_event.delta_y = libinput_event_pointer_get_dy(pevent);
	wlr_signal_emit_safe(&wlr_dev->pointer->events.motion, &wlr_event);
}

void handle_pointer_motion_abs(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_motion_absolute wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_pointer_get_time_usec(pevent));
	wlr_event.x = libinput_event_pointer_get_absolute_x_transformed(pevent, 1);
	wlr_event.y = libinput_event_pointer_get_absolute_y_transformed(pevent, 1);
	wlr_signal_emit_safe(&wlr_dev->pointer->events.motion_absolute, &wlr_event);
}

void handle_pointer_button(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_button wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_pointer_get_time_usec(pevent));
	wlr_event.button = libinput_event_pointer_get_button(pevent);
	switch (libinput_event_pointer_get_button_state(pevent)) {
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event.state = WLR_BUTTON_PRESSED;
		break;
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event.state = WLR_BUTTON_RELEASED;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->pointer->events.button, &wlr_event);
}

void handle_pointer_axis(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_POINTER, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a pointer event for a device with no pointers?");
		return;
	}
	struct libinput_event_pointer *pevent =
		libinput_event_get_pointer_event(event);
	struct wlr_event_pointer_axis wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_pointer_get_time_usec(pevent));
	switch (libinput_event_pointer_get_axis_source(pevent)) {
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
		wlr_event.source = WLR_AXIS_SOURCE_WHEEL;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
		wlr_event.source = WLR_AXIS_SOURCE_FINGER;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
		wlr_event.source = WLR_AXIS_SOURCE_CONTINUOUS;
		break;
	case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL_TILT:
		wlr_event.source = WLR_AXIS_SOURCE_WHEEL_TILT;
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
				wlr_event.orientation = WLR_AXIS_ORIENTATION_VERTICAL;
				break;
			case LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL:
				wlr_event.orientation = WLR_AXIS_ORIENTATION_HORIZONTAL;
				break;
			}
			wlr_event.delta =
				libinput_event_pointer_get_axis_value(pevent, axies[i]);
			wlr_event.delta_discrete =
				libinput_event_pointer_get_axis_value_discrete(pevent, axies[i]);
			wlr_signal_emit_safe(&wlr_dev->pointer->events.axis, &wlr_event);
		}
	}
}
