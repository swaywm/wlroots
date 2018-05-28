#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "util/signal.h"

struct wlr_tablet_tool *create_libinput_tablet_tool(
		struct libinput_device *libinput_dev) {
	assert(libinput_dev);
	struct wlr_tablet_tool *wlr_tablet_tool = calloc(1, sizeof(struct wlr_tablet_tool));
	if (!wlr_tablet_tool) {
		wlr_log(WLR_ERROR, "Unable to allocate wlr_tablet_tool");
		return NULL;
	}
	wlr_tablet_tool_init(wlr_tablet_tool, NULL);
	return wlr_tablet_tool;
}

void handle_tablet_tool_axis(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_axis wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	if (libinput_event_tablet_tool_x_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_X;
		wlr_event.x = libinput_event_tablet_tool_get_x_transformed(tevent, 1);
	}
	if (libinput_event_tablet_tool_y_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_Y;
		wlr_event.y = libinput_event_tablet_tool_get_y_transformed(tevent, 1);
	}
	if (libinput_event_tablet_tool_pressure_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_PRESSURE;
		wlr_event.pressure = libinput_event_tablet_tool_get_pressure(tevent);
	}
	if (libinput_event_tablet_tool_distance_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_DISTANCE;
		wlr_event.distance = libinput_event_tablet_tool_get_distance(tevent);
	}
	if (libinput_event_tablet_tool_tilt_x_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_X;
		wlr_event.tilt_x = libinput_event_tablet_tool_get_tilt_x(tevent);
	}
	if (libinput_event_tablet_tool_tilt_y_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_Y;
		wlr_event.tilt_y = libinput_event_tablet_tool_get_tilt_y(tevent);
	}
	if (libinput_event_tablet_tool_rotation_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_ROTATION;
		wlr_event.rotation = libinput_event_tablet_tool_get_rotation(tevent);
	}
	if (libinput_event_tablet_tool_slider_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_SLIDER;
		wlr_event.slider = libinput_event_tablet_tool_get_slider_position(tevent);
	}
	if (libinput_event_tablet_tool_wheel_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_WHEEL;
		wlr_event.wheel_delta = libinput_event_tablet_tool_get_wheel_delta(tevent);
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.axis, &wlr_event);
}

void handle_tablet_tool_proximity(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_proximity wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	switch (libinput_event_tablet_tool_get_proximity_state(tevent)) {
	case LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT:
		wlr_event.state = WLR_TABLET_TOOL_PROXIMITY_OUT;
		break;
	case LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN:
		wlr_event.state = WLR_TABLET_TOOL_PROXIMITY_IN;
		handle_tablet_tool_axis(event, libinput_dev);
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.proximity, &wlr_event);
}

void handle_tablet_tool_tip(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	handle_tablet_tool_axis(event, libinput_dev);
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_tip wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	switch (libinput_event_tablet_tool_get_tip_state(tevent)) {
	case LIBINPUT_TABLET_TOOL_TIP_UP:
		wlr_event.state = WLR_TABLET_TOOL_TIP_UP;
		break;
	case LIBINPUT_TABLET_TOOL_TIP_DOWN:
		wlr_event.state = WLR_TABLET_TOOL_TIP_DOWN;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.tip, &wlr_event);
}

void handle_tablet_tool_button(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	handle_tablet_tool_axis(event, libinput_dev);
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_button wlr_event = { 0 };
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	wlr_event.button = libinput_event_tablet_tool_get_button(tevent);
	switch (libinput_event_tablet_tool_get_button_state(tevent)) {
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event.state = WLR_BUTTON_RELEASED;
		break;
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event.state = WLR_BUTTON_PRESSED;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.button, &wlr_event);
}
