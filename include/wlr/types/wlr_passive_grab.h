#ifndef WLR_TYPES_WLR_PASSIVE_GRAB_H
#define WLR_TYPES_WLR_PASSIVE_GRAB_H

#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>
#include <wayland-util.h>
#include <wayland-server.h>


struct wlr_passive_grab_grab {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_seat *seat;

	struct wl_list link; // wlr_passive_grab_manager::passive_grabs;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_passive_grab_v1 {
	struct wl_global *global;

	struct wl_list clients; // wlr_passive_grab_manager::link;
	
	struct {
		struct wl_signal new_grab;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_passive_grab_v1 *wlr_passive_grab_create_v1(struct wl_display *display);
void wlr_passive_grab_destroy_v1(struct wlr_passive_grab_v1 *passive_grab);

#endif
