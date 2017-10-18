#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface_layers.h>
#include <wlr/util/log.h>
#include "surface-layers-protocol.h"

static const char *surface_layers_role = "layer_surface";

#define WLR_LAYER_SURFACE_INPUT_DEVICE_COUNT 3

static void layer_surface_set_interactivity(struct wl_client *client,
		struct wl_resource *resource, uint32_t input_types,
		uint32_t exclusive_types) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);

	if (input_types >> WLR_LAYER_SURFACE_INPUT_DEVICE_COUNT) {
		wl_resource_post_error(resource,
			LAYER_SURFACE_ERROR_INVALID_INPUT_DEVICE,
			"Unknown input device in input_types");
		return;
	}
	if (exclusive_types >> WLR_LAYER_SURFACE_INPUT_DEVICE_COUNT) {
		wl_resource_post_error(resource,
			LAYER_SURFACE_ERROR_INVALID_INPUT_DEVICE,
			"Unknown input device in exclusive_types");
		return;
	}

	surface->input_types = input_types;
	surface->exclusive_types = exclusive_types;
	wl_signal_emit(&surface->events.set_interactivity, surface);
}

static void layer_surface_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);

	if (anchor >> 4) {
		wl_resource_post_error(resource, LAYER_SURFACE_ERROR_INVALID_ANCHOR,
			"Unknown anchor");
		return;
	}

	surface->anchor = anchor;
	wl_signal_emit(&surface->events.set_anchor, surface);
}

static void layer_surface_set_exclusive_zone(struct wl_client *client,
		struct wl_resource *resource, uint32_t zone) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	surface->exclusive_zone = zone;
	wl_signal_emit(&surface->events.set_exclusive_zone, surface);
}

static void layer_surface_set_margin(struct wl_client *client,
		struct wl_resource *resource, int32_t horizontal, int32_t vertical) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	surface->margin_horizontal = horizontal;
	surface->margin_vertical = vertical;
	wl_signal_emit(&surface->events.set_margin, surface);
}

void wlr_layer_surface_get_position(struct wlr_layer_surface *layer_surface,
		double *x, double *y) {
	int width = layer_surface->surface->current->width;
	int height = layer_surface->surface->current->height;

	int output_width, output_height;
	wlr_output_effective_resolution(layer_surface->output, &output_width,
		&output_height);

	if (layer_surface->anchor & LAYER_SURFACE_ANCHOR_LEFT) {
		*x = layer_surface->margin_horizontal;
	} else if (layer_surface->anchor & LAYER_SURFACE_ANCHOR_RIGHT) {
		*x = output_width - width - layer_surface->margin_horizontal;
	} else {
		*x = (double)(output_width - width) / 2;
	}
	if (layer_surface->anchor & LAYER_SURFACE_ANCHOR_TOP) {
		*y = layer_surface->margin_vertical;
	} else if (layer_surface->anchor & LAYER_SURFACE_ANCHOR_BOTTOM) {
		*y = output_height - height - layer_surface->margin_vertical;
	} else {
		*y = (double)(output_height - height) / 2;
	}
}

static void layer_surface_destroy(struct wlr_layer_surface *surface) {
	wl_signal_emit(&surface->events.destroy, surface);
	wl_list_remove(&surface->link);
	wl_resource_set_user_data(surface->resource, NULL);
	free(surface);
}

static void layer_surface_resource_destroy(struct wl_resource *resource) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	if (surface != NULL) {
		layer_surface_destroy(surface);
	}
}

static const struct layer_surface_interface layer_surface_impl = {
	.set_interactivity = layer_surface_set_interactivity,
	.set_anchor = layer_surface_set_anchor,
	.set_exclusive_zone = layer_surface_set_exclusive_zone,
	.set_margin = layer_surface_set_margin,
};

static void handle_layer_surface_destroyed(struct wl_listener *listener,
		void *data) {
	struct wlr_layer_surface *surface =
		wl_container_of(listener, surface, surface_destroy_listener);
	layer_surface_destroy(surface);
}

static void surface_layers_get_layer_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *output_resource, uint32_t layer) {
	struct wlr_surface_layers *surface_layers =
		wl_resource_get_user_data(resource);
	struct wlr_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wlr_output *output = wl_resource_get_user_data(output_resource);

	if (layer > SURFACE_LAYERS_LAYER_OVERLAY) {
		wl_resource_post_error(resource, SURFACE_LAYERS_ERROR_INVALID_LAYER,
			"Unknown layer");
		return;
	}
	if (wlr_surface_set_role(surface, surface_layers_role, resource,
			SURFACE_LAYERS_ERROR_ROLE)) {
		return;
	}

	struct wlr_layer_surface *layer_surface =
		calloc(1, sizeof(struct wlr_layer_surface));
	if (layer_surface == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	layer_surface->resource = wl_resource_create(client,
		&layer_surface_interface, wl_resource_get_version(resource), id);
	wlr_log(L_DEBUG, "new layer_surface %p (res %p)", layer_surface,
		layer_surface->resource);
	wl_resource_set_implementation(layer_surface->resource, &layer_surface_impl,
		layer_surface, layer_surface_resource_destroy);

	layer_surface->surface_layers = surface_layers;
	layer_surface->surface = surface;
	layer_surface->output = output;
	layer_surface->layer = layer;

	wl_signal_init(&layer_surface->events.destroy);
	wl_signal_init(&layer_surface->events.set_interactivity);
	wl_signal_init(&layer_surface->events.set_anchor);
	wl_signal_init(&layer_surface->events.set_exclusive_zone);
	wl_signal_init(&layer_surface->events.set_margin);

	wl_signal_add(&layer_surface->surface->events.destroy,
		&layer_surface->surface_destroy_listener);
	layer_surface->surface_destroy_listener.notify =
		handle_layer_surface_destroyed;

	wl_list_insert(&surface_layers->surfaces, &layer_surface->link);
	wl_signal_emit(&surface_layers->events.new_surface, layer_surface);
}

static const struct surface_layers_interface surface_layers_impl = {
	.get_layer_surface = surface_layers_get_layer_surface,
};

static void surface_layers_bind(struct wl_client *wl_client,
		void *_surface_layers, uint32_t version, uint32_t id) {
	struct wlr_screenshooter *surface_layers = _surface_layers;
	assert(wl_client && surface_layers);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported surface_layers version,"
			"disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&surface_layers_interface, version, id);
	wl_resource_set_implementation(wl_resource, &surface_layers_impl,
		surface_layers, NULL);
}

struct wlr_layer_surface *wlr_surface_layers_get_exclusive(
		struct wlr_surface_layers *surface_layers, uint32_t input_devices) {
	struct wlr_layer_surface *layer_surface = NULL;
	int32_t layer = -1;

	struct wlr_layer_surface *_layer_surface;
	wl_list_for_each(_layer_surface, &surface_layers->surfaces, link) {
		if (_layer_surface->exclusive_types & input_devices &&
				(int32_t)_layer_surface->layer > layer) {
			layer = _layer_surface->layer;
			layer_surface = _layer_surface;
		}
	}

	return layer_surface;
}

struct wlr_surface_layers *wlr_surface_layers_create(
		struct wl_display *display) {
	struct wlr_surface_layers *surface_layers =
		calloc(1, sizeof(struct wlr_surface_layers));
	if (!surface_layers) {
		return NULL;
	}

	struct wl_global *wl_global = wl_global_create(display,
		&surface_layers_interface, 1, surface_layers, surface_layers_bind);
	if (!wl_global) {
		free(surface_layers);
		return NULL;
	}
	surface_layers->wl_global = wl_global;

	wl_list_init(&surface_layers->surfaces);

	wl_signal_init(&surface_layers->events.new_surface);

	return surface_layers;
}

void wlr_surface_layers_destroy(struct wlr_surface_layers *surface_layers) {
	if (!surface_layers) {
		return;
	}
	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(surface_layers->wl_global);
	free(surface_layers);
}
