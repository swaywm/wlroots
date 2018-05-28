#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include "util/signal.h"

struct wlr_output_layout_state {
	struct wlr_box _box; // should never be read directly, use the getter
};

struct wlr_output_layout_output_state {
	struct wlr_output_layout *layout;
	struct wlr_output_layout_output *l_output;

	struct wlr_box _box; // should never be read directly, use the getter

	struct wl_listener mode;
	struct wl_listener scale;
	struct wl_listener transform;
	struct wl_listener output_destroy;
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

	struct wlr_output_layout_output *l_output_rel;

	wl_list_for_each(l_output_rel, &l_output->state->layout->outputs, link) {
		if (l_output_rel->reference == l_output) {
			l_output_rel->reference = NULL;
			l_output_rel->configuration =
				WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED;
		}
	}

	wlr_output_destroy_global(l_output->output);
	wl_list_remove(&l_output->state->mode.link);
	wl_list_remove(&l_output->state->scale.link);
	wl_list_remove(&l_output->state->transform.link);
	wl_list_remove(&l_output->state->output_destroy.link);
	wl_list_remove(&l_output->link);
	free(l_output->state);
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

static struct wlr_box *output_layout_output_get_box(
		struct wlr_output_layout_output *l_output) {
	l_output->state->_box.x = l_output->x;
	l_output->state->_box.y = l_output->y;
	int width, height;
	wlr_output_effective_resolution(l_output->output, &width, &height);
	l_output->state->_box.width = width;
	l_output->state->_box.height = height;
	return &l_output->state->_box;
}

/**
 * This must be called whenever the layout changes to reconfigure the auto
 * configured outputs and emit the `changed` event.
 *
 * Auto configured outputs are placed to the right of the north east corner of
 * the rightmost output in the layout in a horizontal line.
 */
static void output_layout_reconfigure(struct wlr_output_layout *layout) {

	struct wlr_output_layout_output *l_output;

	wl_list_for_each(l_output, &layout->outputs, link) {
		int max_x = INT_MIN;
		int max_x_y = INT_MIN;

		struct wlr_box *box = output_layout_output_get_box(l_output);

		switch(l_output->configuration) {
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED:
			break;
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_RELATIVE_LEFT_OF: {
				struct wlr_box *ref_box =
					output_layout_output_get_box(l_output->reference);
				l_output->x = ref_box->x - box->width;
				l_output->y = ref_box->y;
				break;
			}
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_RELATIVE_RIGHT_OF: {
				struct wlr_box *ref_box =
					output_layout_output_get_box(l_output->reference);
				l_output->x = ref_box->x + ref_box->width;
				l_output->y = ref_box->y;
				break;
			}
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_RELATIVE_BELOW: {
				struct wlr_box *ref_box =
					output_layout_output_get_box(l_output->reference);
				l_output->x = ref_box->x;
				l_output->y = ref_box->y + box->height;
				break;
			}
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_RELATIVE_ABOVE: {
				struct wlr_box *ref_box =
					output_layout_output_get_box(l_output->reference);
				l_output->x = ref_box->x;
				l_output->y = ref_box->y - ref_box->height;
				break;
			}
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_RELATIVE_SAME_AS: {
				struct wlr_box *ref_box =
					output_layout_output_get_box(l_output->reference);
				l_output->x = ref_box->x;
				l_output->y = ref_box->y;
				break;
			}
		case WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_AUTO: {
				l_output->x = max_x;
				l_output->y = max_x_y;
				break;
			}
		}

		if (max_x < l_output->x + box->width) {
			max_x = l_output->x + box->width;
			max_x_y = l_output->y;
		}

		struct wlr_output *output = l_output->output;

		wlr_log(L_DEBUG, "%s - %dx%d+%d+%d", output->name, output->width, output->height, l_output->x, l_output->y);
	}

	wl_list_for_each(l_output, &layout->outputs, link) {
		wlr_output_set_position(l_output->output, l_output->x, l_output->y);
	}

	wlr_signal_emit_safe(&layout->events.change, layout);
}

static void handle_output_mode(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output_state *state =
		wl_container_of(listener, state, mode);
	output_layout_reconfigure(state->layout);
}

static void handle_output_scale(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output_state *state =
		wl_container_of(listener, state, scale);
	output_layout_reconfigure(state->layout);
}

static void handle_output_transform(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output_state *state =
		wl_container_of(listener, state, transform);
	output_layout_reconfigure(state->layout);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output_layout_output_state *state =
		wl_container_of(listener, state, output_destroy);
	struct wlr_output_layout *layout = state->layout;
	output_layout_output_destroy(state->l_output);
	output_layout_reconfigure(layout);
}

static struct wlr_output_layout_output *output_layout_output_create(
		struct wlr_output_layout *layout, struct wlr_output *output) {
	struct wlr_output_layout_output *l_output =
		calloc(1, sizeof(struct wlr_output_layout_output));
	if (l_output == NULL) {
		return NULL;
	}
	l_output->state = calloc(1, sizeof(struct wlr_output_layout_output_state));
	if (l_output->state == NULL) {
		free(l_output);
		return NULL;
	}
	l_output->state->l_output = l_output;
	l_output->state->layout = layout;
	l_output->output = output;
	wl_signal_init(&l_output->events.destroy);

	wl_signal_add(&output->events.mode, &l_output->state->mode);
	l_output->state->mode.notify = handle_output_mode;
	wl_signal_add(&output->events.scale, &l_output->state->scale);
	l_output->state->scale.notify = handle_output_scale;
	wl_signal_add(&output->events.transform, &l_output->state->transform);
	l_output->state->transform.notify = handle_output_transform;
	wl_signal_add(&output->events.destroy, &l_output->state->output_destroy);
	l_output->state->output_destroy.notify = handle_output_destroy;

	return l_output;
}

void relocate_output_with_children(struct wlr_output_layout *layout,
		struct wlr_output_layout_output *output,
		struct wl_list *ref) {

	struct wlr_output_layout_output *child_output;
	struct wlr_output_layout_output *parent_output =
		output;

	if (output->link.prev == ref) {
		return;
	}

	for (child_output = wl_container_of((output)->link.next, output, link);
			&output->link != &layout->outputs;
			child_output =
				wl_container_of((child_output)->link.next, output, link)) {
		while (child_output->reference != parent_output) {
			if (parent_output == output) {
				goto move_outputs;
			}
			parent_output = parent_output->reference;
		}
	}

move_outputs:
	ref->next->prev = child_output->link.prev; // A->prev = B
	child_output->link.prev->next = ref->next; // B->next = A
	child_output->link.prev = output->link.prev;
	output->link.prev->next = &child_output->link;
	output->link.prev = ref;
	ref->next = &output->link;
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
		wl_list_insert(&layout->outputs, &l_output->link);
	} else if (l_output->configuration !=
				WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED) {
		relocate_output_with_children(layout, l_output, &layout->outputs);
	}

	l_output->x = lx;
	l_output->y = ly;
	l_output->configuration = WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED;

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
		struct wlr_box *box = output_layout_output_get_box(l_output);
		return wlr_box_contains_point(box, lx, ly);
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
			struct wlr_box *output_box =
				output_layout_output_get_box(l_output);
			if (wlr_box_intersection(output_box, target_lbox, &out_box)) {
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

		struct wlr_box *output_box = output_layout_output_get_box(l_output);
		return wlr_box_intersection(output_box, target_lbox, &out_box);
	}
}

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
		double lx, double ly) {
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		struct wlr_box *box = output_layout_output_get_box(l_output);
		if (wlr_box_contains_point(box, lx, ly)) {
			return l_output->output;
		}
	}
	return NULL;
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
		struct wlr_box *box = output_layout_output_get_box(l_output);
		wlr_box_closest_point(box, lx, ly, &output_x, &output_y);

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

struct wlr_box *wlr_output_layout_get_box(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *l_output;
	if (reference) {
		// output extents
		l_output = wlr_output_layout_get(layout, reference);

		if (l_output) {
			return output_layout_output_get_box(l_output);
		} else {
			return NULL;
		}
	} else {
		// layout extents
		int min_x = INT_MAX, min_y = INT_MAX;
		int max_x = INT_MIN, max_y = INT_MIN;
		wl_list_for_each(l_output, &layout->outputs, link) {
			struct wlr_box *box = output_layout_output_get_box(l_output);

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

		layout->state->_box.x = min_x;
		layout->state->_box.y = min_y;
		layout->state->_box.width = max_x - min_x;
		layout->state->_box.height = max_y - min_y;

		return &layout->state->_box;
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
	} else {
		struct wlr_output_layout_output *l_output_rel;

		wl_list_for_each(l_output_rel, &layout->outputs, link) {
			if (l_output_rel->reference == l_output) {
				l_output_rel->reference = NULL;
				l_output_rel->configuration =
					WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED;
			}
		}
		wl_list_remove(&l_output->link);
	}

	wl_list_insert(layout->outputs.prev, &l_output->link); // Insert at end
	output_layout_reconfigure(layout);
	wlr_output_create_global(output);
	wlr_signal_emit_safe(&layout->events.add, l_output);
}

void wlr_output_layout_add_relative(struct wlr_output_layout *layout,
		struct wlr_output *output, struct wlr_output *reference,
		enum wlr_output_layout_output_configuration configuration) {
	assert(layout && reference);

	if (output == reference) {
		wlr_log(L_ERROR, "Cannot self reference outputs");
		return;
	}

	struct wlr_output_layout_output *l_output_reference =
		wlr_output_layout_get(layout, reference);

	if (!l_output_reference) {
		wlr_output_layout_add_auto(layout, reference);
		l_output_reference = wlr_output_layout_get(layout, reference);
	}
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(layout, output);
	if (!l_output) {
		l_output = output_layout_output_create(layout, output);
		if (!l_output) {
			wlr_log(L_ERROR, "Failed to create wlr_output_layout_output");
			return;
		}
		wl_list_insert(&l_output_reference->link, &l_output->link);
	} else {
		// Prevent loops
		struct wlr_output_layout_output *parent = l_output_reference;

		while (parent) {
			if (parent->reference == l_output) {
				parent->configuration = WLR_OUTPUT_LAYOUT_OUTPUT_CONFIGURATION_FIXED;
				parent->reference = NULL;
				relocate_output_with_children(layout, parent, &layout->outputs);
				break;
			}

			parent = parent->reference;
		}

		relocate_output_with_children(layout, l_output, &l_output_reference->link);
	}

	l_output->configuration = configuration;
	l_output->reference = l_output_reference;

	output_layout_reconfigure(layout);
	wlr_output_create_global(output);
	wlr_signal_emit_safe(&layout->events.add, l_output);
}

struct wlr_output *wlr_output_layout_get_center_output(
		struct wlr_output_layout *layout) {
	if (wl_list_empty(&layout->outputs)) {
		return NULL;
	}

	struct wlr_box *extents = wlr_output_layout_get_box(layout, NULL);
	double center_x = extents->width / 2 + extents->x;
	double center_y = extents->height / 2 + extents->y;

	double dest_x = 0, dest_y = 0;
	wlr_output_layout_closest_point(layout, NULL, center_x, center_y,
		&dest_x, &dest_y);

	return wlr_output_layout_output_at(layout, dest_x, dest_y);
}

struct wlr_output *wlr_output_layout_adjacent_output(
		struct wlr_output_layout *layout, enum wlr_direction direction,
		struct wlr_output *reference, double ref_lx, double ref_ly) {
	assert(reference);

	struct wlr_box *ref_box = wlr_output_layout_get_box(layout, reference);

	double min_distance = DBL_MAX;
	struct wlr_output *closest_output = NULL;
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (reference != NULL && reference == l_output->output) {
			continue;
		}
		struct wlr_box *box = output_layout_output_get_box(l_output);

		bool match = false;
		// test to make sure this output is in the given direction
		if (direction & WLR_DIRECTION_LEFT) {
			match = box->x + box->width <= ref_box->x || match;
		}
		if (direction & WLR_DIRECTION_RIGHT) {
			match = box->x >= ref_box->x + ref_box->width || match;
		}
		if (direction & WLR_DIRECTION_UP) {
			match = box->y + box->height <= ref_box->y || match;
		}
		if (direction & WLR_DIRECTION_DOWN) {
			match = box->y >= ref_box->y + ref_box->height || match;
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
