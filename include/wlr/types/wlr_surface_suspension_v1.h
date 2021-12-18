/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_SURFACE_SUSPENSION_V1
#define WLR_TYPES_WLR_SURFACE_SUSPENSION_V1

#include <wayland-server-core.h>

struct wlr_surface;

struct wlr_surface_suspension_manager_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
	} events;

	// private state

	struct wl_list surfaces;

	struct wl_listener display_destroy;
};

struct wlr_surface_suspension_manager_v1 *
wlr_surface_suspension_manager_v1_create(struct wl_display *display);

void wlr_surface_suspension_manager_v1_set_suspended(
	struct wlr_surface_suspension_manager_v1 *manager,
	struct wlr_surface *surface, bool suspended);

#endif
