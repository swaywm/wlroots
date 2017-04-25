#include <stdint.h>
#include <wayland-client.h>
#include "backend/wayland.h"

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	//struct wlr_wl_seat *seat = data;
	// TODO
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
	struct wlr_wl_seat *seat = data;
	seat->name = name;
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};
