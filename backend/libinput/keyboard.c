#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types.h>
#include <wlr/common/list.h>
#include "backend/libinput.h"
#include "common/log.h"
#include "types.h"

static void wlr_libinput_keyboard_destroy(struct wlr_keyboard_state *state) {
	libinput_device_unref(state->handle);
	free(state);
}

static struct wlr_keyboard_impl keyboard_impl = {
	.destroy = wlr_libinput_keyboard_destroy
};

struct wlr_keyboard *wlr_libinput_keyboard_create(
		struct libinput_device *device) {
	assert(device);
	struct wlr_keyboard_state *kbstate =
		calloc(1, sizeof(struct wlr_keyboard_state));
	kbstate->handle = device;
	libinput_device_ref(device);
	return wlr_keyboard_create(&keyboard_impl, kbstate);
}

void handle_keyboard_key(struct libinput_event *event,
		struct libinput_device *device) {
	struct wlr_input_device *dev =
		get_appropriate_device(WLR_INPUT_DEVICE_KEYBOARD, device);
	if (!dev) {
		wlr_log(L_DEBUG, "Got a keyboard event for a device with no keyboards?");
		return;
	}
	struct libinput_event_keyboard *kbevent =
		libinput_event_get_keyboard_event(event);
	struct wlr_keyboard_key *wlr_event =
		calloc(1, sizeof(struct wlr_keyboard_key));
	wlr_event->time_sec = libinput_event_keyboard_get_time(kbevent);
	wlr_event->time_usec = libinput_event_keyboard_get_time_usec(kbevent);
	wlr_event->keycode = libinput_event_keyboard_get_key(kbevent);
	enum libinput_key_state state = 
		libinput_event_keyboard_get_key_state(kbevent);
	switch (state) {
	case LIBINPUT_KEY_STATE_RELEASED:
		wlr_event->state = WLR_KEY_RELEASED;
		break;
	case LIBINPUT_KEY_STATE_PRESSED:
		wlr_event->state = WLR_KEY_PRESSED;
		break;
	}
	wl_signal_emit(&dev->keyboard->events.key, wlr_event);
}
