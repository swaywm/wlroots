#ifndef TYPES_WLR_SURFACE_H
#define TYPES_WLR_SURFACE_H

#include <wlr/types/wlr_surface.h>

struct wlr_renderer;

/**
 * Create a new surface resource with the provided new ID. If `resource_list`
 * is non-NULL, adds the surface's resource to the list.
 */
struct wlr_surface *surface_create(struct wl_client *client,
	uint32_t version, uint32_t id, struct wlr_renderer *renderer);

#endif
