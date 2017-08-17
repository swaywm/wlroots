#include <wlr/util/log.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <stdlib.h>
#include <assert.h>

struct wlr_output_layout *wlr_output_layout_init() {
	struct wlr_output_layout *layout = calloc(1, sizeof(struct wlr_output_layout));
	wl_list_init(&layout->outputs);
	return layout;
}

void wlr_output_layout_destroy(struct wlr_output_layout *layout) {
	if (!layout) {
		return;
	}

	struct wlr_output_layout_output *_output, *temp = NULL;
	wl_list_for_each_safe(_output, temp, &layout->outputs, link) {
		wl_list_remove(&_output->link);
		free(_output);
	}

	free(layout);
}

void wlr_output_layout_add(struct wlr_output_layout *layout,
		struct wlr_output *output, int x, int y) {
	struct wlr_output_layout_output *layout_output = calloc(1, sizeof(struct wlr_output_layout_output));
	layout_output->output = output;
	layout_output->x = x;
	layout_output->y = y;
	wl_list_insert(&layout->outputs, &layout_output->link);
}

struct wlr_output_layout_output *wlr_output_layout_get(
		struct wlr_output_layout *layout, struct wlr_output *reference) {
	struct wlr_output_layout_output *ret = NULL;
	struct wlr_output_layout_output *_output;
	wl_list_for_each(_output, &layout->outputs, link) {
		if (_output->output == reference) {
			ret = _output;
		}
	}

	return ret;

}

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
		double x, double y) {
	struct wlr_output *ret = NULL;
	struct wlr_output_layout_output *_output;
	wl_list_for_each(_output, &layout->outputs, link) {
		if (_output->output) {
			int width, height;
			wlr_output_effective_resolution(_output->output, &width, &height);
			bool has_x = x >= _output->x && x <= _output->x + width;
			bool has_y = y >= _output->y && y <= _output->y + height;
			if (has_x && has_y) {
				ret = _output->output;
				break;
			}
		}
	}

	return ret;
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
	struct wlr_output_layout_output *layout_output =
			wlr_output_layout_get(layout, output);
	if (layout_output) {
		wl_list_remove(&layout_output->link);
		free(layout_output);
	}
}

void wlr_output_layout_output_coords(struct wlr_output_layout *layout,
		struct wlr_output *reference, int *x, int *y) {
	assert(layout && reference);
	int src_x = *x;
	int src_y = *y;

	struct wlr_output_layout_output *_output;
	wl_list_for_each(_output, &layout->outputs, link) {
		if (_output->output == reference) {
			*x = src_x - _output->x;
			*y = src_y - _output->y;
			return;
		}
	}
}
