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
		uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left) {
	if (exclusive <= 0) {
		return;
	}
	struct {
		uint32_t anchors;
		int *positive_axis;
		int *negative_axis;
		int margin;
	} edges[] = {
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.positive_axis = &usable_area->y,
			.negative_axis = &usable_area->height,
			.margin = margin_top,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->height,
			.margin = margin_bottom,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = &usable_area->x,
			.negative_axis = &usable_area->width,
			.margin = margin_left,
		},
		{
			.anchors =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->width,
			.margin = margin_right,
		},
	};
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
		if ((anchor & edges[i].anchors) == edges[i].anchors) {
			if (edges[i].positive_axis) {
				*edges[i].positive_axis += exclusive + edges[i].margin;
			}
			if (edges[i].negative_axis) {
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			}
		}
	}
}

static void arrange_layer(struct wlr_output *output, struct wl_list *list,
		struct wlr_box *usable_area, bool exclusive) {
	struct roots_layer_surface *roots_surface;
	struct wlr_box full_area = { 0 };
	wlr_output_effective_resolution(output,
			&full_area.width, &full_area.height);
	wl_list_for_each(roots_surface, list, link) {
		struct wlr_layer_surface *layer = roots_surface->layer_surface;
		struct wlr_layer_surface_state *state = &layer->current;
		if (exclusive != (state->exclusive_zone > 0)) {
			continue;
		}
		struct wlr_box bounds;
		if (state->exclusive_zone == -1) {
			bounds = full_area;
		} else {
			bounds = *usable_area;
		}
		struct wlr_box box = {
			.width = state->desired_width,
			.height = state->desired_height
		};
		// Horizontal axis
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		if ((state->anchor & both_horiz) && box.width == 0) {
			box.x = bounds.x;
			box.width = bounds.width;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = bounds.x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = bounds.x + (bounds.width - box.width);
		} else {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}
		// Vertical axis
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		if ((state->anchor & both_vert) && box.height == 0) {
			box.y = bounds.y;
			box.height = bounds.height;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = bounds.y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = bounds.y + (bounds.height - box.height);
		} else {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}
		// Margin
		if ((state->anchor & both_horiz) == both_horiz) {
			box.x += state->margin.left;
			box.width -= state->margin.left + state->margin.right;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x += state->margin.left;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x -= state->margin.right;
		}
		if ((state->anchor & both_vert) == both_vert) {
			box.y += state->margin.top;
			box.height -= state->margin.top + state->margin.bottom;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y += state->margin.top;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y -= state->margin.bottom;
		}
		if (box.width < 0 || box.height < 0) {
			// TODO: Bubble up a protocol error?
			wlr_layer_surface_close(layer);
			continue;
		}
		// Apply
		roots_surface->geo = box;
		apply_exclusive(usable_area, state->anchor, state->exclusive_zone,
				state->margin.top, state->margin.right,
				state->margin.bottom, state->margin.left);
		wlr_layer_surface_configure(layer, box.width, box.height);
	}
}

void arrange_layers(struct roots_output *output) {
	struct wlr_box usable_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);

	// Arrange exclusive surfaces from top->bottom
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, true);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, true);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, true);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, true);
	memcpy(&output->usable_area, &usable_area, sizeof(struct wlr_box));

	struct roots_view *view;
	wl_list_for_each(view, &output->desktop->views, link) {
		if (view->maximized) {
			view_arrange_maximized(view);
		}
	}

	// Arrange non-exlusive surfaces from top->bottom
	usable_area.x = usable_area.y = 0;
	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, false);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, false);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, false);
	arrange_layer(output->wlr_output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, false);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer =
		wl_container_of(listener, layer, output_destroy);
	layer->layer_surface->output = NULL;
	wl_list_remove(&layer->output_destroy.link);
	wlr_layer_surface_close(layer->layer_surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct roots_layer_surface *layer =
		wl_container_of(listener, layer, surface_commit);
	struct wlr_layer_surface *layer_surface = layer->layer_surface;
	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output != NULL) {
		struct roots_output *output = wlr_output->data;
		struct wlr_box old_geo = layer->geo;
		arrange_layers(output);
		if (memcmp(&old_geo, &layer->geo, sizeof(struct wlr_box)) != 0) {
			output_damage_whole_local_surface(output, layer_surface->surface,
					old_geo.x, old_geo.y, 0);
			output_damage_whole_local_surface(output, layer_surface->surface,
					layer->geo.x, layer->geo.y, 0);
		} else {
			output_damage_from_local_surface(output, layer_surface->surface,
					layer->geo.x, layer->geo.y, 0);
		}
	}
}

static void unmap(struct wlr_layer_surface *layer_surface) {
	struct roots_layer_surface *layer = layer_surface->data;
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
	wl_list_remove(&layer->link);
	wl_list_remove(&layer->destroy.link);
	wl_list_remove(&layer->map.link);
	wl_list_remove(&layer->unmap.link);
	wl_list_remove(&layer->surface_commit.link);
	wl_list_remove(&layer->output_destroy.link);
	arrange_layers((struct roots_output *)layer->layer_surface->output->data);
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
	wlr_log(L_DEBUG, "new layer surface: namespace %s layer %d anchor %d "
			"size %dx%d margin %d,%d,%d,%d",
		layer_surface->namespace, layer_surface->layer, layer_surface->layer,
		layer_surface->client_pending.desired_width,
		layer_surface->client_pending.desired_height,
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
	// TODO: Listen for subsurfaces

	roots_surface->layer_surface = layer_surface;
	layer_surface->data = roots_surface;

	struct roots_output *output = layer_surface->output->data;
	wl_list_insert(&output->layers[layer_surface->layer], &roots_surface->link);

	// Temporarily set the layer's current state to client_pending
	// So that we can easily arrange it
	struct wlr_layer_surface_state old_state = layer_surface->current;
	layer_surface->current = layer_surface->client_pending;

	arrange_layers(output);

	layer_surface->current = old_state;
}
