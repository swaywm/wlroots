#ifndef _WLR_BACKEND_WAYLAND_INTERNAL_H
#define _WLR_BACKEND_WAYLAND_INTERNAL_H

struct wlr_wayland_backend {
	struct wl_display *local_display;
	struct wl_display *remote_display;
};

#endif
