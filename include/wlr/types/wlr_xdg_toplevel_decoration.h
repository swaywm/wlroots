#ifndef WLR_TYPES_WLR_XDG_TOPLEVEL_DECORATION
#define WLR_TYPES_WLR_XDG_TOPLEVEL_DECORATION

#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell.h>

enum wlr_xdg_toplevel_decoration_mode {
	WLR_XDG_TOPLEVEL_DECORATION_MODE_CLIENT = 1,
	WLR_XDG_TOPLEVEL_DECORATION_MODE_SERVER = 2,
};

struct wlr_xdg_toplevel_decoration_manager {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list decorations; // wlr_xdg_toplevel_decoration::link

	struct wl_listener display_destroy;

	struct {
		struct wl_signal new_decoration;
	} events;

	void *data;
};

struct wlr_xdg_toplevel_decoration_configure {
	struct wl_list link; // wlr_xdg_toplevel_decoration::configure_list
	struct wlr_xdg_surface_configure *surface_configure;
	enum wlr_xdg_toplevel_decoration_mode mode;
};

struct wlr_xdg_toplevel_decoration {
	struct wl_resource *resource;
	struct wlr_xdg_surface *surface;
	struct wlr_xdg_toplevel_decoration_manager *manager;
	struct wl_list link; // wlr_xdg_toplevel_decoration_manager::link

	bool added;
	enum wlr_xdg_toplevel_decoration_mode current_mode, pending_mode, next_mode;

	struct wl_list configure_list; // wlr_xdg_toplevel_decoration_configure::link

	struct {
		struct wl_signal destroy;
		struct wl_signal request_mode;
	} events;

	struct wl_listener surface_destroy;
	struct wl_listener surface_configure;
	struct wl_listener surface_ack_configure;
	struct wl_listener surface_commit;

	void *data;
};

struct wlr_xdg_toplevel_decoration_manager *
	wlr_xdg_toplevel_decoration_manager_create(struct wl_display *display);
void wlr_xdg_toplevel_decoration_manager_destroy(
	struct wlr_xdg_toplevel_decoration_manager *manager);

void wlr_xdg_toplevel_decoration_send_preferred_mode(
	struct wlr_xdg_toplevel_decoration *decoration,
	enum wlr_xdg_toplevel_decoration_mode mode);
uint32_t wlr_xdg_toplevel_decoration_set_mode(
	struct wlr_xdg_toplevel_decoration *decoration,
	enum wlr_xdg_toplevel_decoration_mode mode);

#endif
