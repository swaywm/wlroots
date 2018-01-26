#ifndef WLR_TYPES_WLR_BOX_H
#define WLR_TYPES_WLR_BOX_H

#include <stdbool.h>
#include <wayland-server.h>

struct wlr_box {
	int x, y;
	int width, height;
};

void wlr_box_closest_point(const struct wlr_box *box, double x, double y,
	double *dest_x, double *dest_y);

bool wlr_box_intersection(const struct wlr_box *box_a,
	const struct wlr_box *box_b, struct wlr_box *dest);

bool wlr_box_contains_point(const struct wlr_box *box, double x, double y);

bool wlr_box_empty(const struct wlr_box *box);

/**
 * Transforms a box inside a `width` x `height` box.
 */
void wlr_box_transform(const struct wlr_box *box,
	enum wl_output_transform transform, int width, int height,
	struct wlr_box *dest);

#endif
