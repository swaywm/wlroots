#ifndef WLR_TYPES_WLR_XWAYLAND_KEYBOARD_GRAB_V1_H
#define WLR_TYPES_WLR_XWAYLAND_KEYBOARD_GRAB_V1_H

#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>
#include <wayland-util.h>
#include <wayland-server.h>

#include <wlr/xwayland.h>


struct wlr_xwayland_keyboard_grab_v1_grab {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_seat *seat;

	struct wl_list link; // wlr_passive_grab_manager::passive_grabs;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_xwayland_keyboard_grab_v1 {
	struct wl_global *global;
	struct wlr_xwayland *xwayland;

	struct wl_list clients; // wlr_passive_grab_manager::link;
	
	struct {
		struct wl_signal new_grab;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_xwayland_keyboard_grab_v1 *wlr_xwayland_keyboard_grab_v1_create(
	struct wl_display *display, struct wlr_xwayland *xwayland);
void wlr_xwayland_keyboard_grab_v1_destroy(struct wlr_xwayland_keyboard_grab_v1 *passive_grab);

#endif
