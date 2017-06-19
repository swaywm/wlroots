#define _XOPEN_SOURCE 500
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include <wlr/types.h>
#include "types.h"
#include "backend/wayland.h"
#include "common/log.h"

static void wlr_wl_device_destroy(struct wlr_input_device_state *state) {
	free(state);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = wlr_wl_device_destroy
};

static struct wlr_input_device *allocate_device(struct wlr_backend_state *state,
		enum wlr_input_device_type type) {
	struct wlr_input_device_state *devstate =
		calloc(1, sizeof(struct wlr_input_device_state));
	if(!devstate) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	// TODO: any way to retrieve those information?
	int vendor = 0;
	int product = 0;
	const char *name = "unknown;wayland";
	struct wlr_input_device *wlr_device = wlr_input_device_create(
		type, &input_device_impl, devstate,
		name, vendor, product);
	if(!wlr_device) {
		free(devstate);
		return NULL;
	}

	list_add(state->devices, wlr_device);
	return wlr_device;
}

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct wlr_backend_state *state = data;
	assert(state->seat == wl_seat);

	// TODO: add listeners and receive input
	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		wlr_log(L_DEBUG, "seat %p offered pointer", wl_seat);
		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);

		struct wlr_input_device *wlr_device = allocate_device(state,
			WLR_INPUT_DEVICE_POINTER);
		if(!wlr_device) {
			wl_pointer_destroy(wl_pointer);
			wlr_log(L_ERROR, "Unable to allocate wl_pointer device");
			return;
		}

		wlr_device->pointer = wlr_pointer_create(NULL, NULL);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		wlr_log(L_DEBUG, "seat %p offered keyboard", wl_seat);
		struct wl_keyboard *wl_keyboard = wl_seat_get_keyboard(wl_seat);
		struct wlr_input_device *wlr_device = allocate_device(state,
			WLR_INPUT_DEVICE_KEYBOARD);
		if(!wlr_device) {
			wl_keyboard_release(wl_keyboard);
			wlr_log(L_ERROR, "Unable to allocate wl_pointer device");
			return;
		}

		wlr_device->keyboard = wlr_keyboard_create(NULL, NULL);
		wl_signal_emit(&state->backend->events.input_add, wlr_device);
	}

	// TODO: touch
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
	struct wlr_backend_state *state = data;
	assert(state->seat == wl_seat);
	state->seatName = strdup(name);
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};
