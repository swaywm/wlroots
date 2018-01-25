#ifndef WLR_UTIL_REGION_H
#define WLR_UTIL_REGION_H

#include <pixman.h>

/**
 * Scales a region, ie. multiplies all its coordinates by `scale`.
 *
 * The resulting coordinates are rounded up or down so that the new region is
 * at least as big as the original one.
 */
void wlr_region_scale(pixman_region32_t *dst, pixman_region32_t *src,
	float scale);

#endif
