#ifndef WLR_TYPES_WLR_SEAT_H
#define WLR_TYPES_WLR_SEAT_H

#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wayland-server.h>

/**
 * Contains state for a single client's bound wl_seat resource and can be used
 * to issue input events to that client. The lifetime of these objects is
 * managed by wlr_seat; some may be NULL.
 */
struct wlr_seat_handle {
	struct wl_resource *wl_resource;
	struct wlr_seat *wlr_seat;

	struct wl_resource *pointer;
	struct wl_resource *keyboard;
	struct wl_resource *touch;
	struct wl_resource *data_device;

	struct wl_list link;
};

struct wlr_seat_pointer_grab;

struct wlr_pointer_grab_interface {
	void (*enter)(struct wlr_seat_pointer_grab *grab,
			struct wlr_surface *surface, double sx, double sy);
	void (*motion)(struct wlr_seat_pointer_grab *grab, uint32_t time,
			double sx, double sy);
	uint32_t (*button)(struct wlr_seat_pointer_grab *grab, uint32_t time,
			uint32_t button, uint32_t state);
	void (*axis)(struct wlr_seat_pointer_grab *grab, uint32_t time,
			enum wlr_axis_orientation orientation, double value);
	void (*cancel)(struct wlr_seat_pointer_grab *grab);
};

struct wlr_seat_keyboard_grab;

struct wlr_keyboard_grab_interface {
	void (*enter)(struct wlr_seat_keyboard_grab *grab, struct wlr_surface *surface);
	void (*key)(struct wlr_seat_keyboard_grab *grab, uint32_t time,
			uint32_t key, uint32_t state);
	void (*modifiers)(struct wlr_seat_keyboard_grab *grab,
			uint32_t mods_depressed, uint32_t mods_latched,
			uint32_t mods_locked, uint32_t group);
	void (*cancel)(struct wlr_seat_keyboard_grab *grab);
};

/**
 * Passed to `wlr_seat_keyboard_start_grab()` to start a grab of the keyboard.
 * The grabber is responsible for handling keyboard events for the seat.
 */
struct wlr_seat_keyboard_grab {
	const struct wlr_keyboard_grab_interface *interface;
	struct wlr_seat *seat;
	void *data;
};

/**
 * Passed to `wlr_seat_pointer_start_grab()` to start a grab of the pointer. The
 * grabber is responsible for handling pointer events for the seat.
 */
struct wlr_seat_pointer_grab {
	const struct wlr_pointer_grab_interface *interface;
	struct wlr_seat *seat;
	void *data;
};

struct wlr_seat_pointer_state {
	struct wlr_seat *wlr_seat;
	struct wlr_seat_handle *focused_handle;
	struct wlr_surface *focused_surface;

	struct wlr_seat_pointer_grab *grab;
	struct wlr_seat_pointer_grab *default_grab;

	uint32_t button_count;
	uint32_t grab_button;
	uint32_t grab_serial;
	uint32_t grab_time;

	struct wl_listener surface_destroy;
	struct wl_listener resource_destroy;
};

struct wlr_seat_keyboard_state {
	struct wlr_seat *wlr_seat;
	struct wlr_keyboard *keyboard;

	struct wlr_seat_handle *focused_handle;
	struct wlr_surface *focused_surface;

	struct wl_listener keyboard_destroy;
	struct wl_listener keyboard_keymap;

	struct wl_listener surface_destroy;
	struct wl_listener resource_destroy;

	struct wlr_seat_keyboard_grab *grab;
	struct wlr_seat_keyboard_grab *default_grab;
};

struct wlr_seat {
	struct wl_global *wl_global;
	struct wl_display *display;
	struct wl_list handles;
	char *name;
	uint32_t capabilities;

	struct wlr_data_device *data_device; // TODO needed?
	struct wlr_data_source *selection_source;
	uint32_t selection_serial;

	struct wlr_seat_pointer_state pointer_state;
	struct wlr_seat_keyboard_state keyboard_state;

	struct wl_listener selection_data_source_destroy;

	struct {
		struct wl_signal client_bound;
		struct wl_signal client_unbound;

		struct wl_signal pointer_grab_begin;
		struct wl_signal pointer_grab_end;

		struct wl_signal keyboard_grab_begin;
		struct wl_signal keyboard_grab_end;

		struct wl_signal request_set_cursor;

		struct wl_signal selection;
	} events;

	void *data;
};

struct wlr_seat_pointer_request_set_cursor_event {
	struct wl_client *client;
	struct wlr_seat_handle *seat_handle;
	struct wlr_surface *surface;
	int32_t hotspot_x, hotspot_y;
};

/**
 * Allocates a new wlr_seat and adds a wl_seat global to the display.
 */
struct wlr_seat *wlr_seat_create(struct wl_display *display, const char *name);
/**
 * Destroys a wlr_seat and removes its wl_seat global.
 */
void wlr_seat_destroy(struct wlr_seat *wlr_seat);
/**
 * Gets a wlr_seat_handle for the specified client, or returns NULL if no
 * handle is bound for that client.
 */
struct wlr_seat_handle *wlr_seat_handle_for_client(struct wlr_seat *wlr_seat,
		struct wl_client *client);
/**
 * Updates the capabilities available on this seat.
 * Will automatically send them to all clients.
 */
void wlr_seat_set_capabilities(struct wlr_seat *wlr_seat,
		uint32_t capabilities);
/**
 * Updates the name of this seat.
 * Will automatically send it to all clients.
 */
void wlr_seat_set_name(struct wlr_seat *wlr_seat, const char *name);

/**
 * Whether or not the surface has pointer focus
 */
bool wlr_seat_pointer_surface_has_focus(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface);

/**
 * Send a pointer enter event to the given surface and consider it to be the
 * focused surface for the pointer. This will send a leave event to the last
 * surface that was entered. Coordinates for the enter event are surface-local.
 * Compositor should use `wlr_seat_pointer_notify_enter()` to change pointer
 * focus to respect pointer grabs.
 */
void wlr_seat_pointer_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy);

/**
 * Clear the focused surface for the pointer and leave all entered surfaces.
 */
void wlr_seat_pointer_clear_focus(struct wlr_seat *wlr_seat);

/**
 * Send a motion event to the surface with pointer focus. Coordinates for the
 * motion event are surface-local. Compositors should use
 * `wlr_seat_pointer_notify_motion()` to send motion events to respect pointer
 * grabs.
 */
void wlr_seat_pointer_send_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy);

/**
 * Send a button event to the surface with pointer focus. Coordinates for the
 * button event are surface-local. Returns the serial. Compositors should use
 * `wlr_seat_pointer_notify_button()` to send button events to respect pointer
 * grabs.
 */
uint32_t wlr_seat_pointer_send_button(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t button, uint32_t state);

/**
 * Send an axis event to the surface with pointer focus. Compositors should use
 * `wlr_seat_pointer_notify_axis()` to send axis events to respect pointer
 * grabs.
 **/
void wlr_seat_pointer_send_axis(struct wlr_seat *wlr_seat, uint32_t time,
		enum wlr_axis_orientation orientation, double value);

/**
 * Start a grab of the pointer of this seat. The grabber is responsible for
 * handling all pointer events until the grab ends.
 */
void wlr_seat_pointer_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_pointer_grab *grab);

/**
 * End the grab of the pointer of this seat. This reverts the grab back to the
 * default grab for the pointer.
 */
void wlr_seat_pointer_end_grab(struct wlr_seat *wlr_seat);

/**
 * Notify the seat of a pointer enter event to the given surface and request it to be the
 * focused surface for the pointer. Pass surface-local coordinates where the
 * enter occurred.
 */
void wlr_seat_pointer_notify_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy);

/**
 * Notify the seat of motion over the given surface. Pass surface-local
 * coordinates where the pointer motion occurred.
 */
void wlr_seat_pointer_notify_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy);

/**
 * Notify the seat that a button has been pressed. Returns the serial of the
 * button press or zero if no button press was sent.
 */
uint32_t wlr_seat_pointer_notify_button(struct wlr_seat *wlr_seat,
		uint32_t time, uint32_t button, uint32_t state);

/**
 * Notify the seat of an axis event.
 */
void wlr_seat_pointer_notify_axis(struct wlr_seat *wlr_seat, uint32_t time,
		enum wlr_axis_orientation orientation, double value);

/**
 * Set this keyboard as the active keyboard for the seat.
 */
void wlr_seat_set_keyboard(struct wlr_seat *seat, struct wlr_input_device *dev);

/**
 * Start a grab of the keyboard of this seat. The grabber is responsible for
 * handling all keyboard events until the grab ends.
 */
void wlr_seat_keyboard_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_keyboard_grab *grab);

/**
 * End the grab of the keyboard of this seat. This reverts the grab back to the
 * default grab for the keyboard.
 */
void wlr_seat_keyboard_end_grab(struct wlr_seat *wlr_seat);

/**
 * Send the keyboard key to focused keyboard resources. Compositors should use
 * `wlr_seat_notify_key()` to respect keyboard grabs.
 */
void wlr_seat_keyboard_send_key(struct wlr_seat *seat, uint32_t time,
		uint32_t key, uint32_t state);

/**
 * Notify the seat that a key has been pressed on the keyboard. Defers to any
 * keyboard grabs.
 */
void wlr_seat_keyboard_notify_key(struct wlr_seat *seat, uint32_t time,
		uint32_t key, uint32_t state);

/**
 * Send the modifier state to focused keyboard resources. Compositors should use
 * `wlr_seat_keyboard_notify_modifiers()` to respect any keyboard grabs.
 */
void wlr_seat_keyboard_send_modifiers(struct wlr_seat *seat,
		uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
		uint32_t group);

/**
 * Notify the seat that the modifiers for the keyboard have changed. Defers to
 * any keyboard grabs.
 */
void wlr_seat_keyboard_notify_modifiers(struct wlr_seat *seat,
		uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked,
		uint32_t group);

/**
 * Notify the seat that the keyboard focus has changed and request it to be the
 * focused surface for this keyboard. Defers to any current grab of the seat's
 * keyboard.
 */
void wlr_seat_keyboard_notify_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface);

/**
 * Send a keyboard enter event to the given surface and consider it to be the
 * focused surface for the keyboard. This will send a leave event to the last
 * surface that was entered. Compositors should use
 * `wlr_seat_keyboard_notify_enter()` to change keyboard focus to respect
 * keyboard grabs.
 */
void wlr_seat_keyboard_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface);

/**
 * Clear the focused surface for the keyboard and leave all entered surfaces.
 */
void wlr_seat_keyboard_clear_focus(struct wlr_seat *wlr_seat);

// TODO: May be useful to be able to simulate keyboard input events

#endif
