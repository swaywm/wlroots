#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_box.h>
#include <wlr/util/log.h>

void wlr_box_closest_point(const struct wlr_box *box, double x, double y,
		double *dest_x, double *dest_y) {
	// if box is empty, then it contains no points, so no closest point either
	if (box->width <= 0 || box->height <= 0) {
		*dest_x = NAN;
		*dest_y = NAN;
		return;
	}

	// find the closest x point
	if (x < box->x) {
		*dest_x = box->x;
	} else if (x >= box->x + box->width) {
		*dest_x = box->x + box->width - 1;
	} else {
		*dest_x = x;
	}

	// find closest y point
	if (y < box->y) {
		*dest_y = box->y;
	} else if (y >= box->y + box->height) {
		*dest_y = box->y + box->height - 1;
	} else {
		*dest_y = y;
	}
}

bool wlr_box_empty(const struct wlr_box *box) {
	return box == NULL || box->width <= 0 || box->height <= 0;
}

bool wlr_box_intersection(struct wlr_box *dest, const struct wlr_box *box_a,
		const struct wlr_box *box_b) {
	bool a_empty = wlr_box_empty(box_a);
	bool b_empty = wlr_box_empty(box_b);

	if (a_empty || b_empty) {
		dest->x = 0;
		dest->y = 0;
		dest->width = -100;
		dest->height = -100;
		return false;
	}

	int x1 = fmax(box_a->x, box_b->x);
	int y1 = fmax(box_a->y, box_b->y);
	int x2 = fmin(box_a->x + box_a->width, box_b->x + box_b->width);
	int y2 = fmin(box_a->y + box_a->height, box_b->y + box_b->height);

	dest->x = x1;
	dest->y = y1;
	dest->width = x2 - x1;
	dest->height = y2 - y1;

	return !wlr_box_empty(dest);
}

bool wlr_box_contains_point(const struct wlr_box *box, double x, double y) {
	if (wlr_box_empty(box)) {
		return false;
	} else {
		return x >= box->x && x < box->x + box->width &&
			y >= box->y && y < box->y + box->height;
	}
}

void wlr_box_transform(struct wlr_box *dest, const struct wlr_box *box,
		enum wl_output_transform transform, int width, int height) {
	struct wlr_box src = *box;

	if (transform % 2 == 0) {
		dest->width = src.width;
		dest->height = src.height;
	} else {
		dest->width = src.height;
		dest->height = src.width;
	}

	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		dest->x = src.x;
		dest->y = src.y;
		break;
	case WL_OUTPUT_TRANSFORM_90:
		dest->x = height - src.y - src.height;
		dest->y = src.x;
		break;
	case WL_OUTPUT_TRANSFORM_180:
		dest->x = width - src.x - src.width;
		dest->y = height - src.y - src.height;
		break;
	case WL_OUTPUT_TRANSFORM_270:
		dest->x = src.y;
		dest->y = width - src.x - src.width;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		dest->x = width - src.x - src.width;
		dest->y = src.y;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		dest->x = src.y;
		dest->y = src.x;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		dest->x = src.x;
		dest->y = height - src.y - src.height;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		dest->x = height - src.y - src.height;
		dest->y = width - src.x - src.width;
		break;
	}
}

void wlr_box_rotated_bounds(struct wlr_box *dest, const struct wlr_box *box,
		float rotation) {
	if (rotation == 0) {
		*dest = *box;
		return;
	}

	double ox = box->x + (double)box->width/2;
	double oy = box->y + (double)box->height/2;

	double c = fabs(cos(rotation));
	double s = fabs(sin(rotation));

	double x1 = ox + (box->x - ox) * c + (box->y - oy) * s;
	double x2 = ox +
		(box->x + box->width - ox) * c +
		(box->y + box->height - oy) * s;

	double y1 = oy + (box->x - ox) * s + (box->y - oy) * c;
	double y2 = oy +
		(box->x + box->width - ox) * s +
		(box->y + box->height - oy) * c;

	dest->x = floor(fmin(x1, x2));
	dest->width = ceil(fmax(x1, x2) - fmin(x1, x2));
	dest->y = floor(fmin(y1, y2));
	dest->height = ceil(fmax(y1, y2) - fmin(y1, y2));
}

void wlr_box_from_pixman_box32(struct wlr_box *dest, const pixman_box32_t box) {
	*dest = (struct wlr_box){
		.x = box.x1,
		.y = box.y1,
		.width = box.x2 - box.x1,
		.height = box.y2 - box.y1,
	};
}
