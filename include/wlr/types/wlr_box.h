#ifndef WLR_TYPES_WLR_BOX_H
#define WLR_TYPES_WLR_BOX_H

#include <stdbool.h>

struct wlr_box {
	int x, y;
	int width, height;
};

void wlr_box_closest_point(struct wlr_box *box, double x, double y,
		double *dest_x, double *dest_y);

bool wlr_box_intersection(struct wlr_box *box_a,
		struct wlr_box *box_b, struct wlr_box **dest);

bool wlr_box_contains_point(struct wlr_box *box, double x, double y);

bool wlr_box_empty(struct wlr_box *box);

enum wl_output_transform;
void wlr_box_transform(struct wlr_box *box, enum wl_output_transform transform,
		struct wlr_box *dest);

#endif
