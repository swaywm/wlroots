#ifndef TYPES_WLR_TABLET_V2_H
#define TYPES_WLR_TABLET_V2_H

#include "tablet-unstable-v2-protocol.h"
#include <wayland-server.h>
#include <wlr/types/wlr_tablet_v2.h>


struct wlr_tablet_client_v2 *tablet_client_from_resource(struct wl_resource *resource);
void destroy_tablet_v2(struct wl_resource *resource);

void destroy_tablet_pad_v2(struct wl_resource *resource);
struct wlr_tablet_pad_client_v2 *tablet_pad_client_from_resource(struct wl_resource *resource);

void destroy_tablet_tool_v2(struct wl_resource *resource);
struct wlr_tablet_tool_client_v2 *tablet_tool_client_from_resource(struct wl_resource *resource);

struct wlr_tablet_seat_client_v2 *tablet_seat_client_from_resource(struct wl_resource *resource);
static void wlr_tablet_seat_client_v2_destroy(struct wl_resource *resource);
#endif /* TYPES_WLR_TABLET_V2_H */
