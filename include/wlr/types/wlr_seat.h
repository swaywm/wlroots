#ifndef _WLR_TYPES_SEAT_H
#define _WLR_TYPES_SEAT_H

#include <wayland-server.h>

struct wlr_keyboard;
struct wlr_pointer;
struct wlr_touch;
struct wlr_data_device_manager;
struct wlr_backend;

struct wlr_seat {
	struct wl_global *global;
	struct wlr_wl_keyboard *keyboard;
	struct wlr_wl_pointer *pointer;
	struct wlr_wl_touch *touch;
	struct wlr_wl_data_device_manager *data_device_manager;

	struct {
		struct wl_listener input_add;
		struct wl_listener input_remove;
	} listener;

	uint32_t caps;
};

struct wlr_seat *wlr_seat_create(struct wl_display *display, struct wlr_backend *backend);
void wlr_seat_destroy(struct wlr_seat *seat);

#endif
