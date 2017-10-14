#ifndef WLR_TYPES_WLR_DATA_DEVICE_H
#define WLR_TYPES_WLR_DATA_DEVICE_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

struct wlr_data_device_manager {
	struct wl_global *global;
};

struct wlr_data_offer {
	struct wl_resource *resource;
	struct wlr_data_source *source;

	uint32_t dnd_actions;
	enum wl_data_device_manager_dnd_action preferred_dnd_action;
	bool in_ask;

	struct wl_listener source_destroy;
};

struct wlr_data_source {
	struct wl_resource *resource;
	struct wlr_data_offer *offer;
	struct wlr_seat_handle *seat;
	struct wl_array mime_types;

	bool accepted;

	// drag and drop
	enum wl_data_device_manager_dnd_action current_dnd_action;
	uint32_t dnd_actions;
	uint32_t compositor_action;
	bool actions_set;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_drag {
	struct wlr_seat_pointer_grab pointer_grab;
	struct wlr_seat_handle *handle;
	struct wlr_seat_handle *focus_handle;

	struct wlr_surface *icon;
	struct wlr_surface *focus;
	struct wlr_data_source *source;

	struct wl_listener icon_destroy;
	struct wl_listener source_destroy;
	struct wl_listener handle_unbound;
};

/**
 * Create a wl data device manager global for this display.
 */
struct wlr_data_device_manager *wlr_data_device_manager_create(
		struct wl_display *display);

/**
 * Creates a new wl_data_offer if there is a wl_data_source currently set as the
 * seat selection and sends it to the client for this handle, followed by the
 * wl_data_device.selection() event.
 * If there is no current selection, the wl_data_device.selection() event will
 * carry a NULL wl_data_offer.
 * If the client does not have a wl_data_device for the seat nothing * will be
 * done.
 */
void wlr_seat_handle_send_selection(struct wlr_seat_handle *handle);

#endif
