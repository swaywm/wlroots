#include <limits.h>
#include <stdlib.h>
#include <math.h>
#include <wlr/types/wlr_geometry.h>

static double get_distance(double x1, double y1, double x2, double y2) {
	double distance;
	distance = sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
	return distance;
}

void wlr_geometry_closest_boundary(struct wlr_geometry *geo, double x, double y,
		int *dest_x, int *dest_y, double *distance) {
	// find the closest x point
	if (x < geo->x) {
		*dest_x = geo->x;
	} else if (x > geo->x + geo->width) {
		*dest_x = geo->x + geo->width;
	} else {
		*dest_x = x;
	}

	// find closest y point
	if (y < geo->y) {
		*dest_y = geo->y;
	} else if (y > geo->y + geo->height) {
		*dest_y = geo->y + geo->height;
	} else {
		*dest_y = y;
	}

	// calculate distance
	if (distance) {
		*distance = get_distance(*dest_x, *dest_y, x, y);
	}
}
