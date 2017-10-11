#ifndef _WLR_SURFACE_LAYERS_H
#define _WLR_SURFACE_LAYERS_H

#include <wayland-server.h>

struct wlr_surface_layers {
	struct wl_global *wl_global;

	void *data;
};

struct wlr_layer_surface {
	struct wl_resource *resource;
	struct wlr_surface_layers *surface_layers;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_surface_layers *wlr_surface_layers_create(
	struct wl_display *display);
void wlr_surface_layers_destroy(struct wlr_surface_layers *surface_layers);

#endif
