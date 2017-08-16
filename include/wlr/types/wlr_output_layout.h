#ifndef _WLR_TYPES_OUTPUT_LAYOUT_H
#define _WLR_TYPES_OUTPUT_LAYOUT_H
#include <wlr/types/wlr_output.h>
#include <wayland-util.h>
#include <stdbool.h>

struct wlr_output_layout {
    struct wl_list outputs;
};

struct wlr_output_layout_output {
    struct wlr_output *output;
    int x, y;
    struct wl_list link;
};

struct wlr_output_layout *wlr_output_layout_init();

void wlr_output_layout_destroy(struct wlr_output_layout *layout);

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
	double x, double y);

void wlr_output_layout_add(struct wlr_output_layout *layout,
    struct wlr_output *output, int x, int y);

void wlr_output_layout_move(struct wlr_output_layout *layout,
    struct wlr_output *output, int x, int y);

void wlr_output_layout_remove(struct wlr_output_layout *layout,
    struct wlr_output *output);

/**
 * Given x and y as pointers to global coordinates, adjusts them to local output
 * coordinates relative to the given reference output.
 */
void wlr_output_layout_output_coords(struct wlr_output_layout *layout,
    struct wlr_output *reference, int *x, int *y);

#endif
