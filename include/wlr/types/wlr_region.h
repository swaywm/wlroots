#ifndef _WLR_TYPES_REGION_H
#define _WLR_TYPES_REGION_H

struct wl_resource;

/*
 * Implements the given resource as region.
 * Sets the associated pixman_region32_t as userdata.
 */
void wlr_region_create(struct wl_client *client, struct wl_resource *res,
		uint32_t id);

#endif
