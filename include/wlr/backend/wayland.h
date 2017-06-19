#ifndef WLR_BACKEND_WAYLAND_H
#define WLR_BACKEND_WAYLAND_H

#include <wayland-client.h>
#include <wayland-server.h>
#include <wlr/backend.h>

struct wlr_backend *wlr_wl_backend_create(struct wl_display *display,
		size_t outputs);

#endif
