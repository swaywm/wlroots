#ifndef WLR_UTIL_REGION_H
#define WLR_UTIL_REGION_H

#include <pixman.h>
#include <wayland-server.h>

/**
 * Scales a region, ie. multiplies all its coordinates by `scale`.
 *
 * The resulting coordinates are rounded up or down so that the new region is
 * at least as big as the original one.
 */
void wlr_region_scale(pixman_region32_t *dst, pixman_region32_t *src,
	float scale);

/**
 * Applies a transform to a region inside a box of size `width` x `height`.
 */
void wlr_region_transform(pixman_region32_t *dst, pixman_region32_t *src,
	enum wl_output_transform transform, int width, int height);

/**
 * Expands the region of `distance`. If `distance` is negative, it shrinks the
 * region.
 */
void wlr_region_expand(pixman_region32_t *dst, pixman_region32_t *src,
	int distance);

#endif
