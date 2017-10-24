#ifndef WLR_TYPES_WLR_SERVER_DECORATION_H
#define WLR_TYPES_WLR_SERVER_DECORATION_H

#include <wayland-server.h>

struct wlr_server_decoration_manager {
	struct wl_global *wl_global;
	struct wl_list decorations; // wlr_server_decoration::link

	void *data;
};

struct wlr_server_decoration {
	struct wl_resource *resource;
	struct wl_list link;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_server_decoration_manager *wlr_server_decoration_manager_create(
	struct wl_display *display);
void wlr_server_decoration_manager_destroy(
	struct wlr_server_decoration_manager *manager);

#endif
