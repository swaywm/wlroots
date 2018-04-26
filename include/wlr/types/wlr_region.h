#ifndef WLR_TYPES_WLR_REGION_H
#define WLR_TYPES_WLR_REGION_H

#include <pixman.h>

struct wl_resource;

/*
 * Creates a new region resource with the provided new ID. If `resource_list` is
 * non-NULL, adds the region's resource to the list.
 */
struct wl_resource *wlr_region_create(struct wl_client *client,
	uint32_t version, uint32_t id, struct wl_list *resource_list);

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
