#ifndef WLR_TYPES_WLR_DATA_DEVICE_H
#define WLR_TYPES_WLR_DATA_DEVICE_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

extern const struct
wlr_pointer_grab_interface wlr_data_device_pointer_drag_interface;

extern const struct
wlr_keyboard_grab_interface wlr_data_device_keyboard_drag_interface;

extern const struct
wlr_touch_grab_interface wlr_data_device_touch_drag_interface;

struct wlr_data_device_manager {
	struct wl_global *global;

	struct wl_listener display_destroy;
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
	struct wlr_seat_client *seat_client;

	struct wl_array mime_types;

	bool accepted;

	// drag and drop
	enum wl_data_device_manager_dnd_action current_dnd_action;
	uint32_t dnd_actions;
	uint32_t compositor_action;
	bool actions_set;

	void (*accept)(struct wlr_data_source *source, uint32_t serial,
			const char *mime_type);
	void (*send)(struct wlr_data_source *source, const char *mime_type,
			int32_t fd);
	void (*cancel)(struct wlr_data_source *source);

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_drag_icon {
	struct wlr_surface *surface;
	struct wlr_seat_client *client;
	struct wl_list link; // wlr_seat::drag_icons
	bool mapped;

	bool is_pointer;
	int32_t touch_id;

	int32_t sx;
	int32_t sy;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
	struct wl_listener seat_client_destroy;
};

struct wlr_drag {
	struct wlr_seat_pointer_grab pointer_grab;
	struct wlr_seat_keyboard_grab keyboard_grab;
	struct wlr_seat_touch_grab touch_grab;

	struct wlr_seat *seat;
	struct wlr_seat_client *seat_client;
	struct wlr_seat_client *focus_client;

	bool is_pointer_grab;

	struct wlr_drag_icon *icon;
	struct wlr_surface *focus;
	struct wlr_data_source *source;

	bool cancelling;
	int32_t grab_touch_id;

	struct wl_listener point_destroy;
	struct wl_listener source_destroy;
	struct wl_listener seat_client_destroy;
	struct wl_listener icon_destroy;
};

/**
 * Create a wl data device manager global for this display.
 */
struct wlr_data_device_manager *wlr_data_device_manager_create(
		struct wl_display *display);

/**
 * Destroys a wlr_data_device_manager and removes its wl_data_device_manager global.
 */
void wlr_data_device_manager_destroy(struct wlr_data_device_manager *manager);

/**
 * Creates a new wl_data_offer if there is a wl_data_source currently set as
 * the seat selection and sends it to the seat client, followed by the
 * wl_data_device.selection() event.  If there is no current selection, the
 * wl_data_device.selection() event will carry a NULL wl_data_offer.  If the
 * client does not have a wl_data_device for the seat nothing * will be done.
 */
void wlr_seat_client_send_selection(struct wlr_seat_client *seat_client);

void wlr_seat_set_selection(struct wlr_seat *seat,
		struct wlr_data_source *source, uint32_t serial);

void wlr_data_source_init(struct wlr_data_source *source);

void wlr_data_source_finish(struct wlr_data_source *source);

#endif
