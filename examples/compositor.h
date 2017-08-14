#ifndef _EXAMPLE_COMPOSITOR_H
#define _EXAMPLE_COMPOSITOR_H
#include <wayland-server.h>
#include <wlr/render.h>

struct wl_compositor_state {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wlr_renderer *renderer;
	struct wl_list surfaces;
	struct wl_listener destroy_surface_listener;
};

void wl_compositor_init(struct wl_display *display,
		struct wl_compositor_state *state, struct wlr_renderer *renderer);

struct wlr_surface;
void wl_compositor_surface_destroyed(struct wl_compositor_state *compositor,
		struct wlr_surface *surface);
#endif
