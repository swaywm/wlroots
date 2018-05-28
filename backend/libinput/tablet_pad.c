#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "util/signal.h"

struct wlr_tablet_pad *create_libinput_tablet_pad(
		struct libinput_device *libinput_dev) {
	assert(libinput_dev);
	struct wlr_tablet_pad *wlr_tablet_pad = calloc(1, sizeof(struct wlr_tablet_pad));
	if (!wlr_tablet_pad) {
		wlr_log(WLR_ERROR, "Unable to allocate wlr_tablet_pad");
		return NULL;
	}
	wlr_tablet_pad_init(wlr_tablet_pad, NULL);
	return wlr_tablet_pad;
}

void handle_tablet_pad_button(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_PAD, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet pad event for a device with no tablet pad?");
		return;
	}
	struct libinput_event_tablet_pad *pevent =
		libinput_event_get_tablet_pad_event(event);
	struct wlr_event_tablet_pad_button wlr_event = { 0 };
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_pad_get_time_usec(pevent));
	wlr_event.button = libinput_event_tablet_pad_get_button_number(pevent);
	wlr_event.mode = libinput_event_tablet_pad_get_mode(pevent);
	switch (libinput_event_tablet_pad_get_button_state(pevent)) {
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event.state = WLR_BUTTON_PRESSED;
		break;
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event.state = WLR_BUTTON_RELEASED;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_pad->events.button, &wlr_event);
}

void handle_tablet_pad_ring(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_PAD, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet pad event for a device with no tablet pad?");
		return;
	}
	struct libinput_event_tablet_pad *pevent =
		libinput_event_get_tablet_pad_event(event);
	struct wlr_event_tablet_pad_ring wlr_event = { 0 };
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_pad_get_time_usec(pevent));
	wlr_event.ring = libinput_event_tablet_pad_get_ring_number(pevent);
	wlr_event.position = libinput_event_tablet_pad_get_ring_position(pevent);
	wlr_event.mode = libinput_event_tablet_pad_get_mode(pevent);
	switch (libinput_event_tablet_pad_get_ring_source(pevent)) {
	case LIBINPUT_TABLET_PAD_RING_SOURCE_UNKNOWN:
		wlr_event.source = WLR_TABLET_PAD_RING_SOURCE_UNKNOWN;
		break;
	case LIBINPUT_TABLET_PAD_RING_SOURCE_FINGER:
		wlr_event.source = WLR_TABLET_PAD_RING_SOURCE_FINGER;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_pad->events.ring, &wlr_event);
}

void handle_tablet_pad_strip(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_PAD, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet pad event for a device with no tablet pad?");
		return;
	}
	struct libinput_event_tablet_pad *pevent =
		libinput_event_get_tablet_pad_event(event);
	struct wlr_event_tablet_pad_strip wlr_event = { 0 };
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_pad_get_time_usec(pevent));
	wlr_event.strip = libinput_event_tablet_pad_get_strip_number(pevent);
	wlr_event.position = libinput_event_tablet_pad_get_strip_position(pevent);
	wlr_event.mode = libinput_event_tablet_pad_get_mode(pevent);
	switch (libinput_event_tablet_pad_get_strip_source(pevent)) {
	case LIBINPUT_TABLET_PAD_STRIP_SOURCE_UNKNOWN:
		wlr_event.source = WLR_TABLET_PAD_STRIP_SOURCE_UNKNOWN;
		break;
	case LIBINPUT_TABLET_PAD_STRIP_SOURCE_FINGER:
		wlr_event.source = WLR_TABLET_PAD_STRIP_SOURCE_FINGER;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_pad->events.strip, &wlr_event);
}
