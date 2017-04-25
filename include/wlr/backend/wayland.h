#ifndef _WLR_BACKEND_WAYLAND_INTERNAL_H
#define _WLR_BACKEND_WAYLAND_INTERNAL_H

#include <wayland-client.h>
#include <wayland-server.h>
#include <wlr/wayland.h>

struct wlr_wl_backend;

void wlr_wl_backend_free(struct wlr_wl_backend *backend);
struct wlr_wl_backend *wlr_wl_backend_init(struct wl_display *display,
		size_t outputs);

#endif
