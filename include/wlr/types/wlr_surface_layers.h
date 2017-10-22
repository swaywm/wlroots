#ifndef _WLR_SURFACE_LAYERS_H
#define _WLR_SURFACE_LAYERS_H

#include <wayland-server.h>
#include <wlr/types/wlr_box.h>

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
	WLR_LAYER_SURFACE_INVALID_INTERACTIVITY = 1 << 0,
	WLR_LAYER_SURFACE_INVALID_ANCHOR = 1 << 1,
	WLR_LAYER_SURFACE_INVALID_EXCLUSIVE_ZONE = 1 << 2,
	WLR_LAYER_SURFACE_INVALID_MARGIN = 1 << 3,
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
	bool configured;
	struct wl_list link; // wlr_surface_layers.surfaces

	struct wlr_layer_surface_state *current, *pending;

	struct wl_listener surface_destroy_listener;
	struct wl_listener surface_commit_listener;
	struct wl_listener output_destroy_listener;

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
void wlr_layer_surface_get_box(struct wlr_layer_surface *layer_surface,
	struct wlr_box *box);

/**
 * Sends a configure event to the layer surface.
 */
void wlr_layer_surface_configure(struct wlr_layer_surface *layer_surface,
	int32_t width, int32_t height);

#endif
