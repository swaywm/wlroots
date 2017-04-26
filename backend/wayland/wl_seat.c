#define _XOPEN_SOURCE 500
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wayland-client.h>
#include "backend/wayland.h"
#include "common/log.h"

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct wlr_wl_seat *seat = data;
	assert(seat->wl_seat == wl_seat);
	struct wlr_wl_backend *backend = wl_seat_get_user_data(wl_seat);
	assert(backend);

	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		wlr_log(L_DEBUG, "seat %p offered pointer", wl_seat);
		struct wl_pointer *wl_pointer = wl_seat_get_pointer(wl_seat);
		struct wlr_wl_pointer *pointer;
		if (!(pointer = calloc(sizeof(struct wlr_wl_pointer), 1))) {
			wl_pointer_destroy(wl_pointer);
			wlr_log(L_ERROR, "Unable to allocate wlr_wl_pointer");
			return;
		}
		pointer->wl_pointer = wl_pointer;
		//wl_pointer_add_listener(wl_pointer, &pointer_listener, backend->registry); TODO
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		wlr_log(L_DEBUG, "seat %p offered keyboard", wl_seat);
		struct wl_keyboard *wl_keyboard = wl_seat_get_keyboard(wl_seat);
		struct wlr_wl_keyboard *keyboard;
		if (!(keyboard = calloc(sizeof(struct wlr_wl_pointer), 1))) {
			wl_keyboard_destroy(wl_keyboard);
			wlr_log(L_ERROR, "Unable to allocate wlr_wl_keyboard");
			return;
		}
		keyboard->wl_keyboard = wl_keyboard;
		//wl_keyboard_add_listener(wl_keyboard, &keyboard_listener, backend->registry); TODO
	}

	// TODO: touch
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
	struct wlr_wl_seat *seat = data;
	assert(seat->wl_seat == wl_seat);
	seat->name = strdup(name);
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};
