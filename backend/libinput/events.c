#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"

struct wlr_input_device *get_appropriate_device(
		enum wlr_input_device_type desired_type,
		struct libinput_device *device) {
	list_t *devices = libinput_device_get_user_data(device);
	if (!devices) {
		return NULL;
	}
	for (size_t i = 0; i < devices->length; ++i) {
		struct wlr_input_device *dev = devices->items[i];
		if (dev->type == desired_type) {
			return dev;
		}
	}
	return NULL;
}

static void wlr_libinput_device_destroy(struct wlr_input_device_state *state) {
	libinput_device_unref(state->handle);
	free(state);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = wlr_libinput_device_destroy
};

static struct wlr_input_device *allocate_device(
		struct wlr_backend_state *state, struct libinput_device *device,
		list_t *devices, enum wlr_input_device_type type) {
	int vendor = libinput_device_get_id_vendor(device);
	int product = libinput_device_get_id_product(device);
	const char *name = libinput_device_get_name(device);
	struct wlr_input_device_state *devstate =
		calloc(1, sizeof(struct wlr_input_device_state));
	devstate->handle = device;
	libinput_device_ref(device);
	struct wlr_input_device *wlr_device = wlr_input_device_create(
		type, &input_device_impl, devstate,
		name, vendor, product);
	list_add(devices, wlr_device);
	list_add(state->devices, wlr_device);
	return wlr_device;
}

static void handle_device_added(struct wlr_backend_state *state,
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
		struct wlr_input_device *wlr_device = allocate_device(state,
				device, devices, WLR_INPUT_DEVICE_KEYBOARD);
		wlr_device->keyboard = wlr_libinput_keyboard_create(device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER)) {
		struct wlr_input_device *wlr_device = allocate_device(state,
				device, devices, WLR_INPUT_DEVICE_POINTER);
		wlr_device->pointer = wlr_libinput_pointer_create(device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TOUCH)) {
		struct wlr_input_device *wlr_device = allocate_device(state,
				device, devices, WLR_INPUT_DEVICE_TOUCH);
		wlr_device->touch = wlr_libinput_touch_create(device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
		struct wlr_input_device *wlr_device = allocate_device(state,
				device, devices, WLR_INPUT_DEVICE_TABLET_TOOL);
		wlr_device->tablet_tool = wlr_libinput_tablet_tool_create(device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
		struct wlr_input_device *wlr_device = allocate_device(state,
				device, devices, WLR_INPUT_DEVICE_TABLET_PAD);
		wlr_device->tablet_pad = wlr_libinput_tablet_pad_create(device);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
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

static void handle_device_removed(struct wlr_backend_state *state,
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
		handle_device_added(state, device);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		handle_device_removed(state, device);
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		handle_keyboard_key(event, device);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		handle_pointer_motion(event, device);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		handle_pointer_motion_abs(event, device);
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		handle_pointer_button(event, device);
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		handle_pointer_axis(event, device);
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		handle_touch_down(event, device);
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		handle_touch_up(event, device);
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		handle_touch_motion(event, device);
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		handle_touch_cancel(event, device);
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		// no-op (at least for now)
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		handle_tablet_tool_axis(event, device);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		handle_tablet_tool_proximity(event, device);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		handle_tablet_tool_tip(event, device);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		handle_tablet_tool_button(event, device);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		handle_tablet_pad_button(event, device);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_RING:
		handle_tablet_pad_ring(event, device);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
		handle_tablet_pad_strip(event, device);
		break;
	default:
		wlr_log(L_DEBUG, "Unknown libinput event %d", event_type);
		break;
	}
}
