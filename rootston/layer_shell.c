#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/layers.h"
#include "rootston/output.h"
#include "rootston/server.h"

static void apply_exclusive(struct wlr_box *usable_area,
		uint32_t anchor, uint32_t exclusive) {
	struct {
		uint32_t anchors;
		int *positive_axis;
		int *negative_axis;
	} edges[] = {
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.positive_axis = &usable_area->y,
			.negative_axis = &usable_area->height,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->height,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = &usable_area->x,
			.negative_axis = &usable_area->width,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->width,
		},
	};
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
		if ((anchor & edges[i].anchors) == edges[i].anchors) {
			if (edges[i].positive_axis) {
				*edges[i].positive_axis += exclusive;
			}
			if (edges[i].negative_axis) {
				*edges[i].negative_axis -= exclusive;
			}
		}
	}
}

static void arrange_layer(struct wlr_output *output, struct wl_list *list,
		struct wlr_box *usable_area) {
	struct roots_layer_surface *roots_surface;
	wl_list_for_each(roots_surface, list, link) {
		struct wlr_layer_surface *layer = roots_surface->layer_surface;
		struct wlr_layer_surface_state *state = &layer->current;
		struct wlr_box box = { .width = state->width, .height = state->height };
		// Horizontal axis
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		if ((state->anchor & both_horiz) && box.width == 0) {
			box.x = 0;
			box.width = usable_area->width;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = usable_area->x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = usable_area->x + (usable_area->width - box.width);
		} else {
			box.x = usable_area->x + ((usable_area->width / 2) - (box.width / 2));
		}
		// Vertical axis
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		if ((state->anchor & both_vert) && box.height == 0) {
			box.y = 0;
			box.height = usable_area->height;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = usable_area->y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = usable_area->y + (usable_area->height - box.height);
		} else {
			box.y = usable_area->y + ((usable_area->height / 2) - (box.height / 2));
		}
		wlr_log(L_DEBUG, "arranged layer at %dx%d@%d,%d",
				box.width, box.height, box.x, box.y);
		roots_surface->geo = box;
		apply_exclusive(usable_area, state->anchor, state->exclusive_zone);
		if (!roots_surface->configured ||
				box.width != (int)state->width ||
				box.height != (int)state->height) {
			wlr_layer_surface_configure(layer, box.width, box.height);
			roots_surface->configured = true;
		}
	}
}

static void arrange_layers(struct wlr_output *_output) {
	struct roots_output *output = _output->data;

	struct wlr_box usable_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);

	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area);
	memcpy(&output->usable_area, &usable_area, sizeof(struct wlr_box));

	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area);

	memset(&usable_area, 0, sizeof(struct wlr_box));
	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);

	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area);

	struct roots_view *view;
	wl_list_for_each(view, &output->desktop->views, link) {
		if (view->maximized) {
			view_arrange_maximized(view);
		}
	}
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer =
		wl_container_of(listener, layer, output_destroy);
	layer->layer_surface->output = NULL;
	wlr_layer_surface_close(layer->layer_surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer =
		wl_container_of(listener, layer, surface_commit);
	struct wlr_layer_surface *layer_surface = layer->layer_surface;
	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output != NULL) {
		struct roots_output *output = wlr_output->data;
		output_damage_from_local_surface(output, layer_surface->surface,
				layer->geo.x, layer->geo.y, 0);
	}
}

static void unmap(struct wlr_layer_surface *layer_surface) {
	struct roots_layer_surface *layer = layer_surface->data;
	if (layer->link.prev) {
		wl_list_remove(&layer->link);
	}

	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output != NULL) {
		struct roots_output *output = wlr_output->data;
		wlr_output_damage_add_box(output->damage, &layer->geo);
	}
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer = wl_container_of(
			listener, layer, destroy);
	if (layer->layer_surface->mapped) {
		unmap(layer->layer_surface);
	}
	wl_list_remove(&layer->output_destroy.link);
	arrange_layers(layer->layer_surface->output);
	free(layer);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface *layer_surface = data;
	struct roots_layer_surface *layer = layer_surface->data;
	struct wlr_output *wlr_output = layer_surface->output;
	struct roots_output *output = wlr_output->data;
	wlr_output_damage_add_box(output->damage, &layer->geo);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer = wl_container_of(
			listener, layer, unmap);
	unmap(layer->layer_surface);
}

void handle_layer_shell_surface(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface *layer_surface = data;
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, layer_shell_surface);
	wlr_log(L_DEBUG, "new layer surface: namespace %s layer %d anchor %d %dx%d %d,%d,%d,%d",
		layer_surface->namespace, layer_surface->layer, layer_surface->layer,
		layer_surface->client_pending.width,
		layer_surface->client_pending.height,
		layer_surface->client_pending.margin.top,
		layer_surface->client_pending.margin.right,
		layer_surface->client_pending.margin.bottom,
		layer_surface->client_pending.margin.left);

	struct roots_layer_surface *roots_surface =
		calloc(1, sizeof(struct roots_layer_surface));
	if (!roots_surface) {
		return;
	}

	roots_surface->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit,
		&roots_surface->surface_commit);

	roots_surface->output_destroy.notify = handle_output_destroy;
	wl_signal_add(&layer_surface->output->events.destroy,
		&roots_surface->output_destroy);

	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&layer_surface->events.destroy, &roots_surface->destroy);
	roots_surface->map.notify = handle_map;
	wl_signal_add(&layer_surface->events.map, &roots_surface->map);
	roots_surface->unmap.notify = handle_unmap;
	wl_signal_add(&layer_surface->events.unmap, &roots_surface->unmap);

	roots_surface->layer_surface = layer_surface;
	layer_surface->data = roots_surface;

	struct roots_output *output = layer_surface->output->data;
	wl_list_insert(&output->layers[layer_surface->layer], &roots_surface->link);

	// Temporarily set the layer's current state to client_pending
	// So that we can easily arrange it
	struct wlr_layer_surface_state old_state = layer_surface->current;
	layer_surface->current = layer_surface->client_pending;

	arrange_layers(output->wlr_output);

	layer_surface->current = old_state;
}
