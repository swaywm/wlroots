#ifndef _EXAMPLE_COMPOSITOR_H
#define _EXAMPLE_COMPOSITOR_H
#include <wayland-server.h>
#include <wlr/render.h>

struct wl_compositor_state {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wlr_renderer *renderer;
	struct wl_list surfaces;
};

void wl_compositor_init(struct wl_display *display,
		struct wl_compositor_state *state, struct wlr_renderer *renderer);

struct wl_shell_state {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
};

void wl_shell_init(struct wl_display *display,
		struct wl_shell_state *state);

#endif
