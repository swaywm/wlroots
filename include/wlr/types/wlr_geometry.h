#ifndef _WLR_TYPES_GEOMETRY_H
#define _WLR_TYPES_GEOMETRY_H
#include <stdbool.h>

struct wlr_geometry {
	int x, y;
	int width, height;
};

void wlr_geometry_closest_boundary(struct wlr_geometry *geo, double x, double y,
		int *dest_x, int *dest_y, double *distance);

bool wlr_geometry_intersection(struct wlr_geometry *geo_a,
		struct wlr_geometry *geo_b, struct wlr_geometry **dest);

bool wlr_geometry_contains_point(struct wlr_geometry *geo, int x, int y);

bool wlr_geometry_empty(struct wlr_geometry *geo);

#endif
