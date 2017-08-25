#ifndef _WLR_TYPES_GEOMETRY_H
#define _WLR_TYPES_GEOMETRY_H

struct wlr_geometry {
	int x, y;
	int width, height;
};

void wlr_geometry_closest_boundary(struct wlr_geometry *geo, double x, double y,
		int *dest_x, int *dest_y, double *distance);

#endif
