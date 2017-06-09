#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/types.h>
#include <wlr/common/list.h>
#include "backend/libinput/backend.h"
#include "common/log.h"
#include "types.h"

static void wlr_libinput_keyboard_destroy(struct wlr_keyboard_state *state) {
	free(state);
}

static struct wlr_keyboard_impl keyboard_impl = {
	.destroy = wlr_libinput_keyboard_destroy
};

static struct wlr_keyboard *wlr_libinput_keyboard_create(
		struct libinput_device *device) {
	assert(device);
	struct wlr_keyboard_state *kbstate =
		calloc(1, sizeof(struct wlr_keyboard_state));
	kbstate->handle = device;
	return wlr_keyboard_create(&keyboard_impl, kbstate);
}

static void device_added(struct wlr_backend_state *state,
		struct libinput_device *device) {
	assert(state && device);
	/*
	 * Note: the wlr API exposes only devices with a single capability, because
	 * that meshes better with how Wayland does things and is a bit simpler.
	 * However, libinput devices often have multiple capabilities - in such
	 * cases we have to create several devices.
	 */
	int vendor = libinput_device_get_id_vendor(device);
	int product = libinput_device_get_id_product(device);
	const char *name = libinput_device_get_name(device);
	list_t *devices = list_create();
	wlr_log(L_DEBUG, "Added %s [%d:%d]", name, vendor, product);

	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
		struct wlr_input_device *wlr_device = wlr_input_device_create(
			WLR_INPUT_DEVICE_KEYBOARD, name, vendor, product);
		wlr_device->keyboard = wlr_libinput_keyboard_create(device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
		list_add(devices, wlr_device);
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER)) {
		// TODO
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TOUCH)) {
		// TODO
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
		// TODO
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
		// TODO
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_GESTURE)) {
		// TODO
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_SWITCH)) {
		// TODO
	}

	if (devices->length > 0) {
		libinput_device_set_user_data(device, devices);
	} else {
		list_free(devices);
	}
}

static void device_removed(struct wlr_backend_state *state,
		struct libinput_device *device) {
	wlr_log(L_DEBUG, "libinput device removed");
	// TODO
}

void wlr_libinput_event(struct wlr_backend_state *state,
		struct libinput_event *event) {
	assert(state && event);
	struct libinput *context = libinput_event_get_context(event);
	struct libinput_device *device = libinput_event_get_device(event);
	enum libinput_event_type event_type = libinput_event_get_type(event);
	(void)context;
	switch (event_type) {
	case LIBINPUT_EVENT_DEVICE_ADDED:
		device_added(state, device);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		device_removed(state, device);
		break;
	default:
		wlr_log(L_DEBUG, "Unknown libinput event %d", event_type);
		break;
	}
}
