#ifndef _WLR_COMPOSITOR_H
#define _WLR_COMPOSITOR_H
#include <wayland-server.h>

struct wlr_compositor_state;

struct wlr_compositor {
	struct wlr_compositor_state *state;
	void *user_data;
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	
	struct {
		/** Emits a reference to the wl_resource just created */
		struct wl_signal bound;
		/** Emits a reference to the wl_surface just created */
		struct wl_signal create_surface;
		/** Emits a reference to the wl_region just created */
		struct wl_signal create_region;
	} events;
};

struct wlr_compositor *wlr_compositor_init(struct wl_display *display);

#endif
