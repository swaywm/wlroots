#ifndef WLR_TYPES_WLR_XDG_OUTPUT_H
#define WLR_TYPES_WLR_XDG_OUTPUT_H
#include <wayland-server.h>
#include <wlr/types/wlr_output_layout.h>

struct wlr_xdg_output {
	struct wlr_xdg_output_manager *manager;
	struct wl_list resources;
	struct wl_list link;

	struct wlr_output_layout_output *layout_output;

	int32_t x, y;
	int32_t width, height;

	struct wl_listener destroy;
};

struct wlr_xdg_output_manager {
	struct wl_global *global;
	struct wl_list resources;
	struct wlr_output_layout *layout;

	struct wl_list outputs;

	struct wl_listener layout_add;
	struct wl_listener layout_change;
	struct wl_listener layout_destroy;
};

struct wlr_xdg_output_manager *wlr_xdg_output_manager_create(
		struct wl_display *display, struct wlr_output_layout *layout);
void wlr_xdg_output_manager_destroy(struct wlr_xdg_output_manager *manager);

#endif
