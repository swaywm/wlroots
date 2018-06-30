#ifndef WLR_TYPES_WLR_COMPOSITOR_H
#define WLR_TYPES_WLR_COMPOSITOR_H

#include <wayland-server.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_surface;

struct wlr_subcompositor {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list subsurface_resources;
};

struct wlr_compositor {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wlr_renderer *renderer;
	struct wl_list surface_resources;
	struct wl_list region_resources;

	struct wlr_subcompositor subcompositor;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal new_surface;
		struct wl_signal destroy;
	} events;
};

void wlr_compositor_destroy(struct wlr_compositor *wlr_compositor);
struct wlr_compositor *wlr_compositor_create(struct wl_display *display,
	struct wlr_renderer *renderer);

bool wlr_surface_is_subsurface(struct wlr_surface *surface);

struct wlr_subsurface *wlr_subsurface_from_wlr_surface(
	struct wlr_surface *surface);

#endif
