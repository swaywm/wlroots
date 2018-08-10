/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_REGION_H
#define WLR_TYPES_WLR_REGION_H

#include <pixman.h>
#include <wayland-server-protocol.h>

/*
 * Creates a new region resource with the provided new ID. If `resource_list` is
 * non-NULL, adds the region's resource to the list.
 */
struct wl_resource *wlr_region_create(struct wl_client *client,
	uint32_t version, uint32_t id, struct wl_list *resource_list);

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
