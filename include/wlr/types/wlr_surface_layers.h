#ifndef _WLR_SURFACE_LAYERS_H
#define _WLR_SURFACE_LAYERS_H

#include <wayland-server.h>

struct wlr_surface_layers {
	struct wl_global *wl_global;

	// list of wlr_layer_surface elements, ordered by ascending layer
	struct wl_list surfaces;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

enum wlr_layer_surface_invalid {
	WLR_LAYER_SURFACE_INVALID_INTERACTIVITY = 1,
	WLR_LAYER_SURFACE_INVALID_ANCHOR = 2,
	WLR_LAYER_SURFACE_INVALID_EXCLUSIVE_ZONE = 4,
	WLR_LAYER_SURFACE_INVALID_MARGIN = 8,
};

struct wlr_layer_surface_state {
	uint32_t invalid; // wlr_layer_surface_invalid
	uint32_t input_types, exclusive_types; // layer_surface_input_device
	uint32_t anchor; // layer_surface_anchor
	uint32_t exclusive_zone;
	int32_t margin_horizontal, margin_vertical;
};

struct wlr_layer_surface {
	struct wl_resource *resource;
	struct wlr_surface_layers *surface_layers;
	struct wlr_surface *surface;
	struct wlr_output *output;
	uint32_t layer; // surface_layers_layer
	struct wl_list link; // wlr_surface_layers.surfaces

	struct wlr_layer_surface_state *current, *pending;

	struct wl_listener surface_destroy_listener;
	struct wl_listener surface_commit_listener;

	struct {
		struct wl_signal destroy;
		struct wl_signal commit;
	} events;

	void *data;
};

struct wlr_surface_layers *wlr_surface_layers_create(
	struct wl_display *display);
void wlr_surface_layers_destroy(struct wlr_surface_layers *surface_layers);

/**
 * Computes the position of the layer surface relative to its output.
 */
void wlr_layer_surface_get_position(struct wlr_layer_surface *layer_surface,
	double *x, double *y);

#endif
