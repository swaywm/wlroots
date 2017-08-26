#include <limits.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <wlr/types/wlr_geometry.h>
#include <wlr/util/log.h>

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

#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

static bool wlr_geometry_empty(struct wlr_geometry *geo) {
	return geo == NULL || geo->width < 0 || geo->height < 0;
}

bool wlr_geometry_intersection(struct wlr_geometry *geo_a,
		struct wlr_geometry *geo_b, struct wlr_geometry **geo_dest) {
	struct wlr_geometry *dest = *geo_dest;
	bool a_empty = wlr_geometry_empty(geo_a);
	bool b_empty = wlr_geometry_empty(geo_b);

	if (a_empty && b_empty) {
		return false;
	} else if (a_empty) {
		dest->x = geo_b->x;
		dest->y = geo_b->y;
		dest->height = geo_b->height;
		dest->width = geo_b->width;
		return true;
	} else if (b_empty) {
		dest->x = geo_a->x;
		dest->y = geo_a->y;
		dest->height = geo_a->height;
		dest->width = geo_a->width;
		return true;
	}

	int x1 = max(geo_a->x, geo_b->x);
	int y1 = max(geo_a->y, geo_b->y);
	int x2 = min(geo_a->x + geo_a->width, geo_b->x + geo_b->width);
	int y2 = min(geo_a->y + geo_a->height, geo_b->y + geo_b->height);

	dest->x = x1;
	dest->y = y1;
	dest->width = x2 - x1;
	dest->height = y2 - y1;

	return !wlr_geometry_empty(dest);
}

bool wlr_geometry_contains_point(struct wlr_geometry *geo, int x, int y) {
	return x >= geo->x && x <= geo->x + geo->width &&
		y >= geo->y && y <= geo->y + geo->height;
}
