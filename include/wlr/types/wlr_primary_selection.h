#ifndef WLR_TYPES_WLR_PRIMARY_SELECTION_H
#define WLR_TYPES_WLR_PRIMARY_SELECTION_H

#include <wayland-server.h>

struct wlr_primary_selection_device_manager {
	struct wl_global *global;

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_primary_selection_source {
	struct wl_resource *resource;

	struct wl_array mime_types;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_primary_selection_device_manager *
	wlr_primary_selection_device_manager_create(struct wl_display *display);
void wlr_primary_selection_device_manager_destroy(
	struct wlr_primary_selection_device_manager *manager);

#endif
