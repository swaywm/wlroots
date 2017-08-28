#include <wlr/util/log.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_geometry.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>

struct wlr_output_layout_state {
	struct wlr_geometry *_geo;
};

struct wlr_output_layout_output_state {
	struct wlr_geometry *_geo;
};

struct wlr_output_layout *wlr_output_layout_init() {
	struct wlr_output_layout *layout = calloc(1, sizeof(struct wlr_output_layout));
	layout->state = calloc(1, sizeof(struct wlr_output_layout_state));
	layout->state->_geo = calloc(1, sizeof(struct wlr_geometry));
	wl_list_init(&layout->outputs);
	return layout;
}

static void wlr_output_layout_output_destroy(
		struct wlr_output_layout_output *l_output) {
		wl_list_remove(&l_output->link);
		free(l_output->state->_geo);
		free(l_output->state);
		free(l_output);
}

void wlr_output_layout_destroy(struct wlr_output_layout *layout) {
	if (!layout) {
		return;
	}

	struct wlr_output_layout_output *_output, *temp = NULL;
	wl_list_for_each_safe(_output, temp, &layout->outputs, link) {
		wlr_output_layout_output_destroy(_output);
	}

	free(layout->state->_geo);
	free(layout->state);
	free(layout);
}

void wlr_output_layout_add(struct wlr_output_layout *layout,
		struct wlr_output *output, int x, int y) {
	struct wlr_output_layout_output *l_output;
	l_output= calloc(1, sizeof(struct wlr_output_layout_output));
	l_output->state = calloc(1, sizeof(struct wlr_output_layout_output_state));
	l_output->state->_geo = calloc(1, sizeof(struct wlr_geometry));
	l_output->output = output;
	l_output->x = x;
	l_output->y = y;
	wl_list_insert(&layout->outputs, &l_output->link);
}

struct wlr_output_layout_output *wlr_output_layout_get(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *_output;
	wl_list_for_each(_output, &layout->outputs, link) {
		if (_output->output == reference) {
			return _output;
		}
	}
	return NULL;
}

static bool output_contains_point( struct wlr_output_layout_output *l_output,
		int x, int y, int width, int height) {
	return x >= l_output->x && x <= l_output->x + width &&
		y >= l_output->y && y <= l_output->y + height;
}

bool wlr_output_layout_contains_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, int x, int y) {
	if (reference) {
		struct wlr_output_layout_output *layout_output = wlr_output_layout_get(layout, reference);
		int width, height;
		wlr_output_effective_resolution(layout_output->output, &width, &height);
		return output_contains_point(layout_output, x, y, width, height);
	} else {
		return !!wlr_output_layout_output_at(layout, x, y);
	}
}

bool wlr_output_layout_intersects(struct wlr_output_layout *layout,
		struct wlr_output *reference, int x1, int y1, int x2, int y2) {
	struct wlr_output_layout_output *l_output = wlr_output_layout_get(layout, reference);
	if (!l_output) {
		return false;
	}
	int width, height;
	wlr_output_effective_resolution(l_output->output, &width, &height);

	// the output must contain one of the points
	return output_contains_point(l_output, x1, y1, width, height) ||
		output_contains_point(l_output, x2, y2, width, height) ||
		output_contains_point(l_output, x2, y1, width, height) ||
		output_contains_point(l_output, y2, x1, width, height);
}

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
		double x, double y) {
	struct wlr_output_layout_output *_output;
	wl_list_for_each(_output, &layout->outputs, link) {
		if (_output->output) {
			int width, height;
			wlr_output_effective_resolution(_output->output, &width, &height);
			bool has_x = x >= _output->x && x <= _output->x + width;
			bool has_y = y >= _output->y && y <= _output->y + height;
			if (has_x && has_y) {
				return _output->output;
			}
		}
	}
	return NULL;
}

void wlr_output_layout_move(struct wlr_output_layout *layout,
		struct wlr_output *output, int x, int y) {
	struct wlr_output_layout_output *layout_output =
			wlr_output_layout_get(layout, output);
	if (layout_output) {
		layout_output->x = x;
		layout_output->y = y;
	}
}

void wlr_output_layout_remove(struct wlr_output_layout *layout,
		struct wlr_output *output) {
	struct wlr_output_layout_output *l_output;
	l_output= wlr_output_layout_get(layout, output);
	if (l_output) {
		wlr_output_layout_output_destroy(l_output);
	}
}

void wlr_output_layout_output_coords(struct wlr_output_layout *layout,
		struct wlr_output *reference, double *x, double *y) {
	assert(layout && reference);
	double src_x = *x;
	double src_y = *y;

	struct wlr_output_layout_output *_output;
	wl_list_for_each(_output, &layout->outputs, link) {
		if (_output->output == reference) {
			*x = src_x - (double)_output->x;
			*y = src_y - (double)_output->y;
			return;
		}
	}
}

static double get_distance(double x1, double y1, double x2, double y2) {
	double distance;
	distance = sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
	return distance;
}

void wlr_output_layout_closest_boundary(struct wlr_output_layout *layout,
		struct wlr_output *reference, double x, double y, double *dest_x,
		double *dest_y) {
	double min_x = INT_MAX, min_y = INT_MAX, min_distance = INT_MAX;
	struct wlr_output_layout_output *l_output;
	wl_list_for_each(l_output, &layout->outputs, link) {
		if (reference != NULL && reference != l_output->output) {
			continue;
		}

		int width, height;
		double output_x, output_y, output_distance;
		wlr_output_effective_resolution(l_output->output, &width, &height);

		// find the closest x point
		// TODO use wlr_geometry_closest_boundary
		if (x < l_output->x) {
			output_x = l_output->x;
		} else if (x > l_output->x + width) {
			output_x = l_output->x + width;
		} else {
			output_x = x;
		}

		// find closest y point
		if (y < l_output->y) {
			output_y = l_output->y;
		} else if (y > l_output->y + height) {
			output_y = l_output->y + height;
		} else {
			output_y = y;
		}

		// calculate distance
		output_distance = get_distance(output_x, output_y, x, y);
		if (output_distance < min_distance) {
			min_x = output_x;
			min_y = output_y;
			min_distance = output_distance;
		}
	}

	*dest_x = min_x;
	*dest_y = min_y;
}

struct wlr_geometry *wlr_output_layout_get_geometry(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *l_output;
	if (reference) {
		// output extents
		l_output= wlr_output_layout_get(layout, reference);
		l_output->state->_geo->x = l_output->x;
		l_output->state->_geo->y = l_output->y;
		wlr_output_effective_resolution(reference,
			&l_output->state->_geo->width, &l_output->state->_geo->height);
		return l_output->state->_geo;
	} else {
		// layout extents
		int min_x = INT_MAX, min_y = INT_MAX;
		int max_x = INT_MIN, max_y = INT_MIN;
		wl_list_for_each(l_output, &layout->outputs, link) {
			int width, height;
			wlr_output_effective_resolution(l_output->output, &width, &height);
			if (l_output->x < min_x) {
				min_x = l_output->x;
			}
			if (l_output->y < min_y) {
				min_y = l_output->y;
			}
			if (l_output->x + width > max_x) {
				max_x = l_output->x + width;
			}
			if (l_output->y + height > max_y) {
				max_y = l_output->y + height;
			}
		}

		layout->state->_geo->x = min_x;
		layout->state->_geo->y = min_y;
		layout->state->_geo->width = max_x - min_x;
		layout->state->_geo->height = max_y - min_y;

		return layout->state->_geo;
	}

	// not reached
}
