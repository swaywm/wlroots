#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"

struct wlr_tablet_tool *wlr_libinput_tablet_tool_create(
		struct libinput_device *device) {
	assert(device);
	return wlr_tablet_tool_create(NULL, NULL);
}

void handle_tablet_tool_axis(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_axis *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_tool_axis));
	wlr_event->time_sec = libinput_event_tablet_tool_get_time(tevent);
	wlr_event->time_usec = libinput_event_tablet_tool_get_time_usec(tevent);
	libinput_device_get_size(device, &wlr_event->width_mm, &wlr_event->height_mm);
	if (libinput_event_tablet_tool_x_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_X;
		wlr_event->x_mm = libinput_event_tablet_tool_get_x(tevent);
	}
	if (libinput_event_tablet_tool_y_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_Y;
		wlr_event->y_mm = libinput_event_tablet_tool_get_y(tevent);
	}
	if (libinput_event_tablet_tool_pressure_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_PRESSURE;
		wlr_event->pressure = libinput_event_tablet_tool_get_pressure(tevent);
	}
	if (libinput_event_tablet_tool_distance_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_DISTANCE;
		wlr_event->distance = libinput_event_tablet_tool_get_distance(tevent);
	}
	if (libinput_event_tablet_tool_tilt_x_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_X;
		wlr_event->tilt_x = libinput_event_tablet_tool_get_tilt_x(tevent);
	}
	if (libinput_event_tablet_tool_tilt_y_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_Y;
		wlr_event->tilt_y = libinput_event_tablet_tool_get_tilt_y(tevent);
	}
	if (libinput_event_tablet_tool_rotation_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_ROTATION;
		wlr_event->rotation = libinput_event_tablet_tool_get_rotation(tevent);
	}
	if (libinput_event_tablet_tool_slider_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_SLIDER;
		wlr_event->slider = libinput_event_tablet_tool_get_slider_position(tevent);
	}
	if (libinput_event_tablet_tool_wheel_has_changed(tevent)) {
		wlr_event->updated_axes |= WLR_TABLET_TOOL_AXIS_WHEEL;
		wlr_event->wheel_delta = libinput_event_tablet_tool_get_wheel_delta(tevent);
	}
	wl_signal_emit(&dev->tablet_tool->events.axis, wlr_event);
}

void handle_tablet_tool_proximity(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_proximity *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_tool_proximity));
	wlr_event->time_sec = libinput_event_tablet_tool_get_time(tevent);
	wlr_event->time_usec = libinput_event_tablet_tool_get_time_usec(tevent);
	switch (libinput_event_tablet_tool_get_proximity_state(tevent)) {
	case LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT:
		wlr_event->state = WLR_TABLET_TOOL_PROXIMITY_OUT;
		break;
	case LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN:
		wlr_event->state = WLR_TABLET_TOOL_PROXIMITY_IN;
		handle_tablet_tool_axis(event, device);
		break;
	}
	wl_signal_emit(&dev->tablet_tool->events.proximity, wlr_event);
}

void handle_tablet_tool_tip(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	handle_tablet_tool_axis(event, device);
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_tip *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_tool_tip));
	wlr_event->time_sec = libinput_event_tablet_tool_get_time(tevent);
	wlr_event->time_usec = libinput_event_tablet_tool_get_time_usec(tevent);
	switch (libinput_event_tablet_tool_get_tip_state(tevent)) {
	case LIBINPUT_TABLET_TOOL_TIP_UP:
		wlr_event->state = WLR_TABLET_TOOL_TIP_UP;
		break;
	case LIBINPUT_TABLET_TOOL_TIP_DOWN:
		wlr_event->state = WLR_TABLET_TOOL_TIP_DOWN;
		break;
	}
	wl_signal_emit(&dev->tablet_tool->events.tip, wlr_event);
}

void handle_tablet_tool_button(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	handle_tablet_tool_axis(event, device);
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_button *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_tool_button));
	wlr_event->time_sec = libinput_event_tablet_tool_get_time(tevent);
	wlr_event->time_usec = libinput_event_tablet_tool_get_time_usec(tevent);
	wlr_event->button = libinput_event_tablet_tool_get_button(tevent);
	switch (libinput_event_tablet_tool_get_button_state(tevent)) {
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event->state = WLR_BUTTON_RELEASED;
		break;
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event->state = WLR_BUTTON_PRESSED;
		break;
	}
	wl_signal_emit(&dev->tablet_tool->events.button, wlr_event);
}
