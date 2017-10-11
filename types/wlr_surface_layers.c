#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface_layers.h>
#include <wlr/util/log.h>
#include "surface-layers-protocol.h"

static const char *surface_layers_role = "layer_surface";

static void layer_surface_set_interactivity(struct wl_client *client,
		struct wl_resource *resource, uint32_t input_types,
		uint32_t exclusive_types) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	surface->input_types = input_types;
	surface->exclusive_types = exclusive_types;
	wl_signal_emit(&surface->events.set_interactivity, surface);
}

static void layer_surface_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
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
		struct wl_resource *resource, uint32_t horizontal,
		uint32_t vertical) {
	struct wlr_layer_surface *surface = wl_resource_get_user_data(resource);
	surface->margin_horizontal = horizontal;
	surface->margin_vertical = vertical;
	wl_signal_emit(&surface->events.set_margin, surface);
}

static void layer_surface_destroy(struct wlr_layer_surface *surface) {
	wl_signal_emit(&surface->events.destroy, surface);
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

static void surface_layers_get_layer_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource,
		struct wl_resource *output_resource, uint32_t layer) {
	struct wlr_surface_layers *surface_layers =
		wl_resource_get_user_data(resource);
	struct wlr_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wlr_output *output = wl_resource_get_user_data(output_resource);

	if (wlr_surface_set_role(surface, surface_layers_role, resource, SURFACE_LAYERS_ERROR_ROLE)) {
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
