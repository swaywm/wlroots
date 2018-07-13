/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_LAYOUT_H
#define WLR_TYPES_WLR_OUTPUT_LAYOUT_H

#include <stdbool.h>
#include <wayland-util.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>

struct wlr_output_layout_state;

struct wlr_output_layout {
	struct wl_list outputs;
	struct wlr_output_layout_state *state;

	struct {
		struct wl_signal add;
		struct wl_signal change;
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_output_layout_output_state;

struct wlr_output_layout_output {
	struct wlr_output *output;
	int x, y;
	struct wl_list link;
	struct wlr_output_layout_output_state *state;

	struct {
		struct wl_signal destroy;
	} events;
};

/**
 * Creates a wlr_output_layout, which can be used to describing outputs in
 * physical space relative to one another, and perform various useful operations
 * on that state.
 */
struct wlr_output_layout *wlr_output_layout_create();

void wlr_output_layout_destroy(struct wlr_output_layout *layout);

struct wlr_output_layout_output *wlr_output_layout_get(
		struct wlr_output_layout *layout, struct wlr_output *reference);

struct wlr_output *wlr_output_layout_output_at(struct wlr_output_layout *layout,
		double lx, double ly);

void wlr_output_layout_add(struct wlr_output_layout *layout,
		struct wlr_output *output, int lx, int ly);

void wlr_output_layout_move(struct wlr_output_layout *layout,
		struct wlr_output *output, int lx, int ly);

void wlr_output_layout_remove(struct wlr_output_layout *layout,
		struct wlr_output *output);

/**
 * Given x and y in layout coordinates, adjusts them to local output
 * coordinates relative to the given reference output.
 */
void wlr_output_layout_output_coords(struct wlr_output_layout *layout,
		struct wlr_output *reference, double *lx, double *ly);

bool wlr_output_layout_contains_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, int lx, int ly);

bool wlr_output_layout_intersects(struct wlr_output_layout *layout,
		struct wlr_output *reference, const struct wlr_box *target_lbox);

/**
 * Get the closest point on this layout from the given point from the reference
 * output. If reference is NULL, gets the closest point from the entire layout.
 */
void wlr_output_layout_closest_point(struct wlr_output_layout *layout,
		struct wlr_output *reference, double lx, double ly, double *dest_lx,
		double *dest_ly);

/**
 * Get the box of the layout for the given reference output in layout
 * coordinates. If `reference` is NULL, the box will be for the extents of the
 * entire layout.
 */
struct wlr_box *wlr_output_layout_get_box(
		struct wlr_output_layout *layout, struct wlr_output *reference);

/**
* Add an auto configured output to the layout. This will place the output in a
* sensible location in the layout. The coordinates of the output in the layout
* may adjust dynamically when the layout changes. If the output is already in
* the layout, it will become auto configured. If the position of the output is
* set such as with `wlr_output_layout_move()`, the output will become manually
* configured.
*/
void wlr_output_layout_add_auto(struct wlr_output_layout *layout,
		struct wlr_output *output);

/**
 * Get the output closest to the center of the layout extents.
 */
struct wlr_output *wlr_output_layout_get_center_output(
		struct wlr_output_layout *layout);

enum wlr_direction {
	WLR_DIRECTION_UP = 1,
	WLR_DIRECTION_DOWN = 2,
	WLR_DIRECTION_LEFT = 4,
	WLR_DIRECTION_RIGHT = 8,
};

/**
 * Get the closest adjacent output to the reference output from the reference
 * point in the given direction.
 */
struct wlr_output *wlr_output_layout_adjacent_output(
		struct wlr_output_layout *layout, enum wlr_direction direction,
		struct wlr_output *reference, double ref_lx, double ref_ly);

#endif
