#ifndef _WLR_TYPES_SEAT_H
#define _WLR_TYPES_SEAT_H
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

	struct wl_list link;
};

struct wlr_seat {
	struct wl_global *wl_global;
	struct wl_list handles;
	char *name;
	uint32_t capabilities;

	struct {
		struct wl_signal client_bound;
		struct wl_signal client_unbound;
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
 */
void wlr_seat_set_capabilities(struct wlr_seat *wlr_seat, uint32_t capabilities);
/**
 * Updates the name of this seat.
 */
void wlr_seat_set_name(struct wlr_seat *wlr_seat, const char *name);

#endif
