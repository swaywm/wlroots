/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_TRANSIENT_SEAT_V1_H
#define WLR_TYPES_WLR_TRANSIENT_SEAT_V1_H

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

struct wlr_seat;

struct wlr_transient_seat_manager_v1 {
	struct wl_global *global;
	struct wl_listener display_destroy;

	struct wlr_seat *(*find_seat)(const char *name);
	struct wlr_seat *(*create_seat)(const char *name);
	void (*destroy_seat)(struct wlr_seat *seat);

	struct wl_list transient_seats;
};

struct wlr_transient_seat_v1 {
	struct wlr_seat *seat;
	struct wl_resource *resource;
	struct wlr_transient_seat_manager_v1 *manager;
	struct wl_list link;
};

struct wlr_transient_seat_manager_v1 *wlr_transient_seat_manager_v1_create(
		struct wl_display *display);

#endif /* WLR_TYPES_WLR_TRANSIENT_SEAT_V1_H */
