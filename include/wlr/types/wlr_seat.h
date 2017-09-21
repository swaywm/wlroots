#ifndef _WLR_TYPES_SEAT_H
#define _WLR_TYPES_SEAT_H
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_input_device.h>
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

struct wlr_seat_pointer_state {
	struct wlr_seat *wlr_seat;
	struct wlr_seat_handle *focused_handle;
	struct wlr_surface *focused_surface;

	struct wl_listener focus_surface_destroy_listener;
	struct wl_listener focus_resource_destroy_listener;
};

struct wlr_seat {
	struct wl_global *wl_global;
	struct wl_display *display;
	struct wl_list handles;
	char *name;
	uint32_t capabilities;
	struct wlr_data_device *data_device;

	struct wlr_seat_pointer_state pointer_state;

	struct {
		struct wl_signal client_bound;
		struct wl_signal client_unbound;
		struct wl_signal keyboard_bound;
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
 */
void wlr_seat_pointer_enter(struct wlr_seat *wlr_seat,
		struct wlr_surface *surface, double sx, double sy);

/**
 * Clear the focused surface for the pointer and leave all entered surfaces.
 */
void wlr_seat_pointer_clear_focus(struct wlr_seat *wlr_seat);

/**
 * Send a motion event to the surface with pointer focus. Coordinates for the
 * motion event are surface-local.
 */
void wlr_seat_pointer_send_motion(struct wlr_seat *wlr_seat, uint32_t time,
		double sx, double sy);

/**
 * Send a button event to the surface with pointer focus. Coordinates for the
 * button event are surface-local.
 */
void wlr_seat_pointer_send_button(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t button, uint32_t state);

#endif
