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

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
