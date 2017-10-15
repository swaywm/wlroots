#ifndef _WLR_SURFACE_LAYERS_H
#define _WLR_SURFACE_LAYERS_H

#include <wayland-server.h>

struct wlr_surface_layers {
	struct wl_global *wl_global;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_layer_surface {
	struct wl_resource *resource;
	struct wlr_surface_layers *surface_layers;
	struct wlr_surface *surface;
	struct wlr_output *output;
	uint32_t layer;

	uint32_t input_types, exclusive_types;
	uint32_t anchor;
	uint32_t exclusive_zone;
	uint32_t margin_horizontal, margin_vertical;

	struct wl_listener surface_destroy_listener;

	struct {
		struct wl_signal destroy;
		struct wl_signal set_interactivity;
		struct wl_signal set_anchor;
		struct wl_signal set_exclusive_zone;
		struct wl_signal set_margin;
	} events;

	void *data;
};

struct wlr_surface_layers *wlr_surface_layers_create(
	struct wl_display *display);
void wlr_surface_layers_destroy(struct wlr_surface_layers *surface_layers);

#endif
