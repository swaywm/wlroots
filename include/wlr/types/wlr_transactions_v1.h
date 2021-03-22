/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_TRANSACTIONS_V1_H
#define WLR_TYPES_WLR_TRANSACTIONS_V1_H

#include <wayland-server-core.h>

struct wlr_transaction_v1_surface {
	struct wlr_surface *surface;
	struct wl_list link; // wlr_transactions_v1.surfaces
	uint32_t cached_state_seq;

	struct wl_listener surface_destroy;
};

struct wlr_transaction_v1 {
	struct wl_resource *resource;

	struct wl_list surfaces; // wlr_transaction_v1_surface.link
};

struct wlr_transaction_manager_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_transaction_manager_v1 *wlr_transaction_manager_v1_create(
	struct wl_display *display);

#endif
