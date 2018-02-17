#ifndef WLR_TYPES_WLR_REGION_H
#define WLR_TYPES_WLR_REGION_H

#include <pixman.h>

struct wl_resource;

/*
 * Implements the given resource as region.
 */
void wlr_region_create(struct wl_client *client, struct wl_resource *res,
	uint32_t id);

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
