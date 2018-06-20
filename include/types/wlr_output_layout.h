#ifndef TYPES_WLR_OUTPUT_LAYOUT_H
#define TYPES_WLR_OUTPUT_LAYOUT_H

#include <wlr/types/wlr_output_layout.h>

struct wlr_output_layout_output {
	struct wlr_output_layout *layout;
	struct wlr_output *output;
	int x, y;
	struct wl_list link;
	bool auto_configured;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener mode;
	struct wl_listener scale;
	struct wl_listener transform;
	struct wl_listener output_destroy;
};

struct wlr_output_layout_output *wlr_output_layout_get(
		struct wlr_output_layout *layout, struct wlr_output *reference);

#endif
