/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_DATA_DEVICE_H
#define WLR_TYPES_WLR_DATA_DEVICE_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

extern const struct wlr_pointer_grab_interface
	wlr_data_device_pointer_drag_interface;

extern const struct wlr_keyboard_grab_interface
	wlr_data_device_keyboard_drag_interface;

extern const struct wlr_touch_grab_interface
	wlr_data_device_touch_drag_interface;

struct wlr_data_device_manager {
	struct wl_global *global;
	struct wl_list resources;
	struct wl_list data_sources;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_data_offer {
	struct wl_resource *resource;
	struct wlr_data_source *source;

	uint32_t actions;
	enum wl_data_device_manager_dnd_action preferred_action;
	bool in_ask;

	struct wl_listener source_destroy;
};

/**
 * A data source implementation. Only the `send` function is mandatory. Refer to
 * the matching wl_data_source_* functions documentation to know what they do.
 */
struct wlr_data_source_impl {
	void (*send)(struct wlr_data_source *source, const char *mime_type,
		int32_t fd);
	void (*accept)(struct wlr_data_source *source, uint32_t serial,
		const char *mime_type);
	void (*cancel)(struct wlr_data_source *source);

	void (*dnd_drop)(struct wlr_data_source *source);
	void (*dnd_finish)(struct wlr_data_source *source);
	void (*dnd_action)(struct wlr_data_source *source,
		enum wl_data_device_manager_dnd_action action);
};

struct wlr_data_source {
	const struct wlr_data_source_impl *impl;

	// source metadata
	struct wl_array mime_types;
	int32_t actions;

	// source status
	bool accepted;

	// drag'n'drop status
	enum wl_data_device_manager_dnd_action current_dnd_action;
	uint32_t compositor_action;

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

	struct {
		struct wl_signal map;
		struct wl_signal unmap;
		struct wl_signal destroy;
	} events;

	struct wl_listener surface_destroy;
	struct wl_listener seat_client_destroy;

	void *data;
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

	struct {
		struct wl_signal focus;
		struct wl_signal motion;
		struct wl_signal drop;
		struct wl_signal destroy;
	} events;
};

struct wlr_drag_motion_event {
	struct wlr_drag *drag;
	uint32_t time;
	double sx, sy;
};

struct wlr_drag_drop_event {
	struct wlr_drag *drag;
	uint32_t time;
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

/**
 * Sets the current selection for the seat. NULL can be provided to clear it.
 * This removes the previous one if there was any. In case the selection doesn't
 * come from a client, wl_display_next_serial() can be used to generate a
 * serial.
 */
void wlr_seat_set_selection(struct wlr_seat *seat,
	struct wlr_data_source *source, uint32_t serial);

/**
 * Initializes the data source with the provided implementation.
 */
void wlr_data_source_init(struct wlr_data_source *source,
	const struct wlr_data_source_impl *impl);

/**
 * Finishes the data source.
 */
void wlr_data_source_finish(struct wlr_data_source *source);

/**
 * Sends the data as the specified MIME type over the passed file descriptor,
 * then close it.
 */
void wlr_data_source_send(struct wlr_data_source *source, const char *mime_type,
	int32_t fd);

/**
 * Notifies the data source that a target accepts one of the offered MIME types.
 * If a target doesn't accept any of the offered types, `mime_type` is NULL.
 */
void wlr_data_source_accept(struct wlr_data_source *source, uint32_t serial,
	const char *mime_type);

/**
 * Notifies the data source it is no longer valid and should be destroyed. That
 * potentially destroys immediately the data source.
 */
void wlr_data_source_cancel(struct wlr_data_source *source);

/**
 * Notifies the data source that the drop operation was performed. This does not
 * indicate acceptance.
 *
 * The data source may still be used in the future and should not be destroyed
 * here.
 */
void wlr_data_source_dnd_drop(struct wlr_data_source *source);

/**
 * Notifies the data source that the drag-and-drop operation concluded. That
 * potentially destroys immediately the data source.
 */
void wlr_data_source_dnd_finish(struct wlr_data_source *source);

/**
 * Notifies the data source that a target accepts the drag with the specified
 * action.
 *
 * This shouldn't be called after `wlr_data_source_dnd_drop` unless the
 * drag-and-drop operation ended in an "ask" action.
 */
void wlr_data_source_dnd_action(struct wlr_data_source *source,
	enum wl_data_device_manager_dnd_action action);

#endif
