/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_GTK_PRIMARY_SELECTION_H
#define WLR_TYPES_WLR_GTK_PRIMARY_SELECTION_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

struct wlr_gtk_primary_selection_device_manager {
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link
	struct wl_list devices; // wlr_gtk_primary_selection_device::link

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

/**
 * A device is a per-seat object used to set and get the current selection.
 */
struct wlr_gtk_primary_selection_device {
	struct wlr_gtk_primary_selection_device_manager *manager;
	struct wlr_seat *seat;
	struct wl_list link; // wlr_gtk_primary_selection_device_manager::devices
	struct wl_list resources; // wl_resource_get_link

	struct wl_list offers; // wl_resource_get_link
	uint32_t selection_serial;

	struct wl_listener seat_destroy;
	struct wl_listener seat_focus_change;
	struct wl_listener seat_primary_selection;

	void *data;
};

struct wlr_gtk_primary_selection_device_manager *
	wlr_gtk_primary_selection_device_manager_create(struct wl_display *display);
void wlr_gtk_primary_selection_device_manager_destroy(
	struct wlr_gtk_primary_selection_device_manager *manager);

#endif
