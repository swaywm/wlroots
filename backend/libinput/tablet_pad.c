#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"

struct wlr_tablet_pad *wlr_libinput_tablet_pad_create(
		struct libinput_device *device) {
	assert(device);
	return wlr_tablet_pad_create(NULL, NULL);
}

void handle_tablet_pad_button(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_PAD, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet pad event for a device with no tablet pad?");
		return;
	}
	struct libinput_event_tablet_pad *pevent =
		libinput_event_get_tablet_pad_event(event);
	struct wlr_event_tablet_pad_button *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_pad_button));
	wlr_event->time_sec = libinput_event_tablet_pad_get_time(pevent);
	wlr_event->time_usec = libinput_event_tablet_pad_get_time_usec(pevent);
	wlr_event->button = libinput_event_tablet_pad_get_button_number(pevent);
	switch (libinput_event_tablet_pad_get_button_state(pevent)) {
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event->state = WLR_BUTTON_PRESSED;
		break;
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event->state = WLR_BUTTON_RELEASED;
		break;
	}
	wl_signal_emit(&dev->tablet_pad->events.button, wlr_event);
}

void handle_tablet_pad_ring(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_PAD, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet pad event for a device with no tablet pad?");
		return;
	}
	struct libinput_event_tablet_pad *pevent =
		libinput_event_get_tablet_pad_event(event);
	struct wlr_event_tablet_pad_ring *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_pad_ring));
	wlr_event->time_sec = libinput_event_tablet_pad_get_time(pevent);
	wlr_event->time_usec = libinput_event_tablet_pad_get_time_usec(pevent);
	wlr_event->ring = libinput_event_tablet_pad_get_ring_number(pevent);
	wlr_event->position = libinput_event_tablet_pad_get_ring_position(pevent);
	switch (libinput_event_tablet_pad_get_ring_source(pevent)) {
	case LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN:
		wlr_event->source = WLR_TABLET_PAD_RING_SOURCE_UNKNOWN;
		break;
	case LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER:
		wlr_event->source = WLR_TABLET_PAD_RING_SOURCE_FINGER;
		break;
	}
	wl_signal_emit(&dev->tablet_pad->events.ring, wlr_event);
}

void handle_tablet_pad_strip(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_PAD, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a tablet pad event for a device with no tablet pad?");
		return;
	}
	struct libinput_event_tablet_pad *pevent =
		libinput_event_get_tablet_pad_event(event);
	struct wlr_event_tablet_pad_strip *wlr_event =
		calloc(1, sizeof(struct wlr_event_tablet_pad_strip));
	wlr_event->time_sec = libinput_event_tablet_pad_get_time(pevent);
	wlr_event->time_usec = libinput_event_tablet_pad_get_time_usec(pevent);
	wlr_event->strip = libinput_event_tablet_pad_get_strip_number(pevent);
	wlr_event->position = libinput_event_tablet_pad_get_strip_position(pevent);
	switch (libinput_event_tablet_pad_get_strip_source(pevent)) {
	case LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN:
		wlr_event->source = WLR_TABLET_PAD_STRIP_SOURCE_UNKNOWN;
		break;
	case LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER:
		wlr_event->source = WLR_TABLET_PAD_STRIP_SOURCE_FINGER;
		break;
	}
	wl_signal_emit(&dev->tablet_pad->events.strip, wlr_event);
}
