#ifndef WLR_TYPES_WLR_DATA_DEVICE_H
#define WLR_TYPES_WLR_DATA_DEVICE_H

#include <wayland-server.h>

struct wlr_data_device_manager {
	struct wl_global *global;
};

struct wlr_data_offer {
	struct wl_resource *resource;
};

struct wlr_data_source {
	struct wl_resource *resource;
	struct wlr_data_offer *offer;
	struct wlr_seat_handle *seat;
	struct wl_array mime_types;

	struct {
		struct wl_signal destroy;
	} events;
};

/**
 * Create a wl data device manager global for this display.
 */
struct wlr_data_device_manager *wlr_data_device_manager_create(
		struct wl_display *display);

#endif
