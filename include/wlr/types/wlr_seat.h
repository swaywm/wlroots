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
	struct wlr_seat_keyboard *seat_keyboard;

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

	struct wl_listener surface_destroy;
	struct wl_listener resource_destroy;

	struct wlr_seat_pointer_grab *grab;
	struct wlr_seat_pointer_grab *default_grab;
};

struct wlr_seat_keyboard {
	struct wlr_seat *seat;
	struct wlr_keyboard *keyboard;
	struct wl_listener key;
	struct wl_listener modifiers;
	struct wl_listener keymap;
	struct wl_listener destroy;
	struct wl_list link;
};

struct wlr_seat_keyboard_state {
	struct wlr_seat *wlr_seat;
	struct wlr_seat_handle *focused_handle;
	struct wlr_surface *focused_surface;

	struct wl_listener surface_destroy;
	struct wl_listener resource_destroy;
};

struct wlr_seat {
	struct wl_global *wl_global;
	struct wl_display *display;
	struct wl_list handles;
	struct wl_list keyboards;
	char *name;
	uint32_t capabilities;
	struct wlr_data_device *data_device;

	struct wlr_seat_pointer_state pointer_state;
	struct wlr_seat_keyboard_state keyboard_state;

	struct {
		struct wl_signal client_bound;
		struct wl_signal client_unbound;

		struct wl_signal pointer_grab_begin;
		struct wl_signal pointer_grab_end;
	} events;

	void *data;
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
 * Attaches this keyboard to the seat. Key events from this keyboard will be
 * propegated to the focused client.
 */
void wlr_seat_attach_keyboard(struct wlr_seat *seat,
		struct wlr_input_device *dev);

/**
 * Detaches this keyboard from the seat. This is done automatically when the
 * keyboard is destroyed; you only need to use this if you want to remove it for
 * some other reason.
 */
void wlr_seat_detach_keyboard(struct wlr_seat *seat, struct wlr_keyboard *kb);

/**
 * Send a keyboard enter event to the given surface and consider it to be the
 * focused surface for the keyboard. This will send a leave event to the last
 * surface that was entered. Pass an array of currently pressed keys.
 */
void wlr_seat_keyboard_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface);

/**
 * Clear the focused surface for the keyboard and leave all entered surfaces.
 */
void wlr_seat_keyboard_clear_focus(struct wlr_seat *wlr_seat);

// TODO: May be useful to be able to simulate keyboard input events

#endif
