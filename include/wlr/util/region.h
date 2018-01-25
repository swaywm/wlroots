#ifndef WLR_UTIL_REGION_H
#define WLR_UTIL_REGION_H

#include <pixman.h>

void wlr_region_scale(pixman_region32_t *dst, pixman_region32_t *src,
	float scale);

#endif
