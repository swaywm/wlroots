#ifndef _EXAMPLE_COMPOSITOR_H
#define _EXAMPLE_COMPOSITOR_H
#include <wayland-server.h>
#include <wlr/render.h>

struct wlr_compositor {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wlr_renderer *renderer;
	struct wl_list surfaces;
	struct wl_listener destroy_surface_listener;

	struct wl_signal create_surface_signal;
};

void wlr_compositor_destroy(struct wlr_compositor *wlr_compositor);
struct wlr_compositor *wlr_compositor_create(struct wl_display *display,
		struct wlr_renderer *renderer);

struct wlr_surface;
void wl_compositor_surface_destroyed(struct wlr_compositor *wlr_compositor,
		struct wlr_surface *surface);
#endif
