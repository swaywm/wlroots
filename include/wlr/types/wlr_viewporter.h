/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_VIEWPORTER_H
#define WLR_TYPES_WLR_VIEWPORTER_H

#include <wayland-server-core.h>

struct wlr_viewporter {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_viewport {
	struct wl_resource *resource;
	struct wlr_surface *surface;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
};

struct wlr_viewporter *wlr_viewporter_create(struct wl_display *display);

#endif
