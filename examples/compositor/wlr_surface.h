#ifndef _EXAMPLES_COMPOSITOR_SURFACE_H
#define _EXAMPLES_COMPOSITOR_SURFACE_H

#include <wayland-server.h>

struct wlr_surface {
	struct wl_resource *pending_buffer;
	bool pending_attached;
	bool attached; // whether the surface currently has a buffer attached

	struct wlr_texture *texture;
	const char *role; // the lifetime-bound role or null
	struct wl_resource *resource;

	struct {
		struct wl_signal destroy;
		struct wl_signal commit;
	} signals;
};

struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer);

#endif
