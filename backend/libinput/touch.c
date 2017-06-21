#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"

struct wlr_touch *wlr_libinput_touch_create(
		struct libinput_device *device) {
	assert(device);
	return wlr_touch_create(NULL, NULL);
}

void handle_touch_down(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TOUCH, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a touch event for a device with no touch?");
		return;
	}
	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	struct wlr_event_touch_down *wlr_event =
		calloc(1, sizeof(struct wlr_event_touch_down));
	wlr_event->time_sec = libinput_event_touch_get_time(tevent);
	wlr_event->time_usec = libinput_event_touch_get_time_usec(tevent);
	wlr_event->slot = libinput_event_touch_get_slot(tevent);
	wlr_event->x_mm = libinput_event_touch_get_x(tevent);
	wlr_event->y_mm = libinput_event_touch_get_y(tevent);
	libinput_device_get_size(device, &wlr_event->width_mm, &wlr_event->height_mm);
	wl_signal_emit(&dev->touch->events.down, wlr_event);
}

void handle_touch_up(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TOUCH, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a touch event for a device with no touch?");
		return;
	}
	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	struct wlr_event_touch_up *wlr_event =
		calloc(1, sizeof(struct wlr_event_touch_up));
	wlr_event->time_sec = libinput_event_touch_get_time(tevent);
	wlr_event->time_usec = libinput_event_touch_get_time_usec(tevent);
	wlr_event->slot = libinput_event_touch_get_slot(tevent);
	wl_signal_emit(&dev->touch->events.up, wlr_event);
}

void handle_touch_motion(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TOUCH, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a touch event for a device with no touch?");
		return;
	}
	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	struct wlr_event_touch_motion *wlr_event =
		calloc(1, sizeof(struct wlr_event_touch_motion));
	wlr_event->time_sec = libinput_event_touch_get_time(tevent);
	wlr_event->time_usec = libinput_event_touch_get_time_usec(tevent);
	wlr_event->slot = libinput_event_touch_get_slot(tevent);
	wlr_event->x_mm = libinput_event_touch_get_x(tevent);
	wlr_event->y_mm = libinput_event_touch_get_y(tevent);
	libinput_device_get_size(device, &wlr_event->width_mm, &wlr_event->height_mm);
	wl_signal_emit(&dev->touch->events.motion, wlr_event);
}

void handle_touch_cancel(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TOUCH, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a touch event for a device with no touch?");
		return;
	}
	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	struct wlr_event_touch_cancel *wlr_event =
		calloc(1, sizeof(struct wlr_event_touch_cancel));
	wlr_event->time_sec = libinput_event_touch_get_time(tevent);
	wlr_event->time_usec = libinput_event_touch_get_time_usec(tevent);
	wlr_event->slot = libinput_event_touch_get_slot(tevent);
	wl_signal_emit(&dev->touch->events.cancel, wlr_event);
}
