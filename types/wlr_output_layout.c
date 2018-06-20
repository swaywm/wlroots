#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "types/wlr_output_layout.h"

struct wlr_output_layout_state {
	struct wlr_box _box; // should never be read directly, use the getter
};

struct wlr_output_layout *wlr_output_layout_create(void) {
	struct wlr_output_layout *layout =
		calloc(1, sizeof(struct wlr_output_layout));
	if (layout == NULL) {
		return NULL;
	}
	layout->state = calloc(1, sizeof(struct wlr_output_layout_state));
	if (layout->state == NULL) {
		free(layout);
		return NULL;
	}
	wl_list_init(&layout->outputs);

	wl_signal_init(&layout->events.add);
	wl_signal_init(&layout->events.change);
	wl_signal_init(&layout->events.destroy);

	return layout;
}

static void output_layout_output_destroy(
		struct wlr_output_layout_output *l_output) {
	wlr_signal_emit_safe(&l_output->events.destroy, l_output);
	wlr_output_destroy_global(l_output->output);
	wl_list_remove(&l_output->mode.link);
	wl_list_remove(&l_output->scale.link);
	wl_list_remove(&l_output->transform.link);
	wl_list_remove(&l_output->output_destroy.link);
	wl_list_remove(&l_output->link);
	free(l_output);
}

void wlr_output_layout_destroy(struct wlr_output_layout *layout) {
	if (!layout) {
		return;
	}

	wlr_signal_emit_safe(&layout->events.destroy, layout);

	struct wlr_output_layout_output *l_output, *temp;
	wl_list_for_each_safe(l_output, temp, &layout->outputs, link) {
		output_layout_output_destroy(l_output);
	}

	free(layout->state);
	free(layout);
}

void output_layout_output_get_box(
		struct wlr_output_layout_output *l_output, struct wlr_box *box) {
	assert(l_output != NULL);
	box->x = l_output->x;
	box->y = l_output->y;
	int width, height;
	wlr_output_effective_resolution(l_output->output, &width, &height);
	box->width = width;
	box->height = height;
}

/**
 * This must be called whenever the layout changes to reconfigure the auto
 * configured outputs and emit the `changed` event.
 *
 * Auto configured outputs are placed to the right of the north east corner of
 * the rightmost output in the layout in a horizontal line.
 */
static void output_layout_reconfigure(struct wlr_output_layout *layout) {
	int max_x = INT_MIN;
	int max_x_y = INT_MIN; // y value for the max_x output

	// find the rightmost x coordinate occupied by a manually configured output
	// in the layout
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->auto_configured) {
			continue;
		}

		struct wlr_box box;
		output_layout_output_get_box(l_output, &box);
		if (box.x + box.width > max_x) {
			max_x = box.x + box.width;
			max_x_y = box.y;
		}
	}

	if (max_x == INT_MIN) {
		// there are no manually configured outputs
		max_x = 0;
		max_x_y = 0;
	}

	wl_list_for_each(l_output, &layout->outputs, link) {
		if (!l_output->auto_configured) {
			continue;
		}
		struct wlr_box box;
		output_layout_output_get_box(l_output, &box);
		l_output->x = max_x;
		l_output->y = max_x_y;
		max_x += box.width;
	}

	wl_list_for_each(l_output, &layout->outputs, link) {
		wlr_output_set_position(l_output->output, l_output->x, l_output->y);
	}

	wlr_signal_emit_safe(&layout->events.change, layout);
}

static void handle_output_mode(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output *l_output =
		wl_container_of(listener, l_output, mode);
	output_layout_reconfigure(l_output->layout);
}

static void handle_output_scale(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output *l_output =
		wl_container_of(listener, l_output, scale);
	output_layout_reconfigure(l_output->layout);
}

static void handle_output_transform(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output *l_output =
		wl_container_of(listener, l_output, transform);
	output_layout_reconfigure(l_output->layout);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output *l_output =
		wl_container_of(listener, l_output, output_destroy);
	struct wlr_output_layout *layout = l_output->layout;
	output_layout_output_destroy(l_output);
	output_layout_reconfigure(layout);
}

static struct wlr_output_layout_output *output_layout_output_create(
		struct wlr_output_layout *layout, struct wlr_output *output) {
	struct wlr_output_layout_output *l_output =
		calloc(1, sizeof(struct wlr_output_layout_output));
	if (l_output == NULL) {
		return NULL;
	}

	l_output->layout = layout;
	l_output->output = output;
	wl_signal_init(&l_output->events.destroy);
	wl_list_insert(&layout->outputs, &l_output->link);

	wl_signal_add(&output->events.mode, &l_output->mode);
	l_output->mode.notify = handle_output_mode;
	wl_signal_add(&output->events.scale, &l_output->scale);
	l_output->scale.notify = handle_output_scale;
	wl_signal_add(&output->events.transform, &l_output->transform);
	l_output->transform.notify = handle_output_transform;
	wl_signal_add(&output->events.destroy, &l_output->output_destroy);
	l_output->output_destroy.notify = handle_output_destroy;

	return l_output;
}

void wlr_output_layout_add(struct wlr_output_layout *layout,
		struct wlr_output *output, int lx, int ly) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (!l_output) {
		l_output = output_layout_output_create(layout, output);
		if (!l_output) {
			wlr_log(L_ERROR, "Failed to create wlr_output_layout_output");
			return;
		}
	}
	l_output->x = lx;
	l_output->y = ly;
	l_output->auto_configured = false;
	output_layout_reconfigure(layout);
	wlr_output_create_global(output);
	wlr_signal_emit_safe(&layout->events.add, l_output);
}

struct wlr_output_layout_output *wlr_output_layout_get(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->output == reference) {
			return l_output;
		}
	}
	return NULL;
}

bool wlr_output_layout_contains_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, int lx, int ly) {
	if (reference) {
		struct wlr_output_layout_output *l_output =
			wlr_output_layout_get(layout, reference);

		if (l_output == NULL) {
			return false;
		}

		struct wlr_box box;
		output_layout_output_get_box(l_output, &box);
		return wlr_box_contains_point(&box, lx, ly);
	} else {
		return !!wlr_output_layout_output_at(layout, lx, ly);
	}
}

bool wlr_output_layout_intersects(struct wlr_output_layout *layout,
		struct wlr_output *reference, const struct wlr_box *target_lbox) {
	struct wlr_box out_box;

	if (reference == NULL) {
		struct wlr_output_layout_output *l_output;
		wl_list_for_each(l_output, &layout->outputs, link) {
			struct wlr_box output_box;
			output_layout_output_get_box(l_output, &output_box);
			if (wlr_box_intersection(&output_box, target_lbox, &out_box)) {
				return true;
			}
		}
		return false;
	} else {
		struct wlr_output_layout_output *l_output =
			wlr_output_layout_get(layout, reference);
		if (!l_output) {
			return false;
		}

		struct wlr_box output_box;
		output_layout_output_get_box(l_output, &output_box);
		return wlr_box_intersection(&output_box, target_lbox, &out_box);
	}
}

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
		double lx, double ly) {
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		struct wlr_box box;
		output_layout_output_get_box(l_output, &box);
		if (wlr_box_contains_point(&box, lx, ly)) {
			return l_output->output;
		}
	}
	return NULL;
}

void wlr_output_layout_move(struct wlr_output_layout *layout,
		struct wlr_output *output, int lx, int ly) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (l_output) {
		l_output->x = lx;
		l_output->y = ly;
		l_output->auto_configured = false;
		output_layout_reconfigure(layout);
	} else {
		wlr_log(L_ERROR, "output not found in this layout: %s", output->name);
	}
}

void wlr_output_layout_remove(struct wlr_output_layout *layout,
		struct wlr_output *output) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (l_output) {
		output_layout_output_destroy(l_output);
		output_layout_reconfigure(layout);
	}
}

void wlr_output_layout_output_coords(struct wlr_output_layout *layout,
		struct wlr_output *reference, double *lx, double *ly) {
	assert(layout && reference);
	double src_x = *lx;
	double src_y = *ly;

	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (l_output->output == reference) {
			*lx = src_x - (double)l_output->x;
			*ly = src_y - (double)l_output->y;
			return;
		}
	}
}

void wlr_output_layout_closest_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, double lx, double ly, double *dest_lx,
		double *dest_ly) {
	if (dest_lx == NULL && dest_ly == NULL) {
		return;
	}

	double min_x = DBL_MAX, min_y = DBL_MAX, min_distance = DBL_MAX;
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (reference != NULL && reference != l_output->output) {
			continue;
		}

		double output_x, output_y, output_distance;
		struct wlr_box box;
		output_layout_output_get_box(l_output, &box);
		wlr_box_closest_point(&box, lx, ly, &output_x, &output_y);

		// calculate squared distance suitable for comparison
		output_distance =
			(lx - output_x) * (lx - output_x) + (ly - output_y) * (ly - output_y);

		if (!isfinite(output_distance)) {
			output_distance = DBL_MAX;
		}

		if (output_distance <= min_distance) {
			min_x = output_x;
			min_y = output_y;
			min_distance = output_distance;
		}
	}

	if (dest_lx) {
		*dest_lx = min_x;
	}
	if (dest_ly) {
		*dest_ly = min_y;
	}
}

bool wlr_output_layout_get_box(struct wlr_output_layout *layout,
		struct wlr_output *reference, struct wlr_box *box) {
	assert(layout != NULL);

	if (wl_list_empty(&layout->outputs)) {
		return false;
	}

	struct wlr_output_layout_output *l_output;
	if (reference) {
		// output extents
		l_output = wlr_output_layout_get(layout, reference);

		if (l_output) {
			output_layout_output_get_box(l_output, box);
			return true;
		} else {
			return false;
		}
	} else {
		// layout extents
		int min_x = INT_MAX, min_y = INT_MAX;
		int max_x = INT_MIN, max_y = INT_MIN;
		wl_list_for_each(l_output, &layout->outputs, link) {
			struct wlr_box output_box;
			output_layout_output_get_box(l_output, &output_box);

			if (box->x < min_x) {
				min_x = box->x;
			}
			if (box->y < min_y) {
				min_y = box->y;
			}
			if (box->x + box->width > max_x) {
				max_x = box->x + box->width;
			}
			if (box->y + box->height > max_y) {
				max_y = box->y + box->height;
			}
		}

		box->x = min_x;
		box->y = min_y;
		box->width = max_x - min_x;
		box->height = max_y - min_y;

		return true;
	}

	// not reached
}

void wlr_output_layout_add_auto(struct wlr_output_layout *layout,
		struct wlr_output *output) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (!l_output) {
		l_output = output_layout_output_create(layout, output);
		if (!l_output) {
			wlr_log(L_ERROR, "Failed to create wlr_output_layout_output");
			return;
		}
	}

	l_output->auto_configured = true;
	output_layout_reconfigure(layout);
	wlr_output_create_global(output);
	wlr_signal_emit_safe(&layout->events.add, l_output);
}

struct wlr_output *wlr_output_layout_get_center_output(
		struct wlr_output_layout *layout) {
	struct wlr_box extents;
	if (!wlr_output_layout_get_box(layout, NULL, &extents)) {
		return NULL;
	}
	double center_x = extents.width / 2 + extents.x;
	double center_y = extents.height / 2 + extents.y;

	double dest_x = 0, dest_y = 0;
	wlr_output_layout_closest_point(layout, NULL, center_x, center_y,
		&dest_x, &dest_y);

	return wlr_output_layout_output_at(layout, dest_x, dest_y);
}


struct wlr_output *wlr_output_layout_adjacent_output(
		struct wlr_output_layout *layout, enum wlr_direction direction,
		struct wlr_output *reference, double ref_lx, double ref_ly) {
	assert(reference);

	struct wlr_box ref_box;
	if (!wlr_output_layout_get_box(layout, reference, &ref_box)) {
		// empty layout or invalid reference
		return NULL;
	}

	double min_distance = DBL_MAX;
	struct wlr_output *closest_output = NULL;
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (reference != NULL && reference == l_output->output) {
			continue;
		}
		struct wlr_box box;
		output_layout_output_get_box(l_output, &box);

		bool match = false;
		// test to make sure this output is in the given direction
		if (direction & WLR_DIRECTION_LEFT) {
			match = box.x + box.width <= ref_box.x || match;
		}
		if (direction & WLR_DIRECTION_RIGHT) {
			match = box.x >= ref_box.x + ref_box.width || match;
		}
		if (direction & WLR_DIRECTION_UP) {
			match = box.y + box.height <= ref_box.y || match;
		}
		if (direction & WLR_DIRECTION_DOWN) {
			match = box.y >= ref_box.y + ref_box.height || match;
		}
		if (!match) {
			continue;
		}

		// calculate distance from the given reference point
		double x, y;
		wlr_output_layout_closest_point(layout, l_output->output,
			ref_lx, ref_ly, &x, &y);
		double distance =
			(x - ref_lx) * (x - ref_lx) + (y - ref_ly) * (y - ref_ly);
		if (distance < min_distance) {
			min_distance = distance;
			closest_output = l_output->output;
		}
	}
	return closest_output;
}
