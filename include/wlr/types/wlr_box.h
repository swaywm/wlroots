/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_BOX_H
#define WLR_TYPES_WLR_BOX_H

#include <pixman.h>
#include <stdbool.h>
#include <wayland-server-protocol.h>

struct wlr_box {
	int x, y;
	int width, height;
};

struct wlr_fbox {
	double x, y;
	double width, height;
};

void wlr_box_closest_point(const struct wlr_box *box, double x, double y,
	double *dest_x, double *dest_y);

bool wlr_box_intersection(struct wlr_box *dest, const struct wlr_box *box_a,
	const struct wlr_box *box_b);

bool wlr_box_contains_point(const struct wlr_box *box, double x, double y);

bool wlr_box_empty(const struct wlr_box *box);

/**
 * Transforms a box inside a `width` x `height` box.
 */
void wlr_box_transform(struct wlr_box *dest, const struct wlr_box *box,
	enum wl_output_transform transform, int width, int height);

/**
 * Creates the smallest box that contains the box rotated about its center.
 */
void wlr_box_rotated_bounds(struct wlr_box *dest, const struct wlr_box *box, float rotation);

void wlr_box_from_pixman_box32(struct wlr_box *dest, const pixman_box32_t box);

#endif
