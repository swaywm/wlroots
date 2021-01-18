#ifndef TYPES_WLR_REGION_H
#define TYPES_WLR_REGION_H

#include <wlr/types/wlr_region.h>

/*
 * Creates a new region resource with the provided new ID. If `resource_list` is
 * non-NULL, adds the region's resource to the list.
 */
struct wl_resource *region_create(struct wl_client *client,
	uint32_t version, uint32_t id);

#endif
