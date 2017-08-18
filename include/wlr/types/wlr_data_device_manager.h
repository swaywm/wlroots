#ifndef _WLR_TYPES_DATA_DEVICE_MANAGER_H
#define _WLR_TYPES_DATA_DEVICE_MANAGER_H

#include <wayland-server.h>

struct wlr_data_device_manager {
	struct wl_global *global;
};

struct wlr_data_device_manager *wlr_data_device_manager_create(struct wl_display *dpy);
void wlr_data_device_manager_destroy(struct wlr_data_device_manager *manager);

struct wlr_data_device {
	struct wlr_seat *seat;
	struct wlr_data_source *selection;
	struct wl_listener selection_destroyed;

	struct {
		struct wl_signal selection_change;
	} events;
};

void wlr_data_device_set_selection(struct wlr_data_device *manager,
		struct wlr_data_source *source);

#endif
