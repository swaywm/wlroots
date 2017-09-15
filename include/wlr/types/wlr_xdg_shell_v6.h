#ifndef _WLR_XDG_SHELL_V6_H
#define _WLR_XDG_SHELL_V6_H
#include <wlr/types/wlr_box.h>
#include <wayland-server.h>

struct wlr_xdg_shell_v6 {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;

	void *data;
};

enum wlr_xdg_surface_v6_role {
	WLR_XDG_SURFACE_V6_ROLE_NONE,
	WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL,
	WLR_XDG_SURFACE_V6_ROLE_POPUP,
};

struct wlr_xdg_toplevel_v6_state {
	bool maximized;
	bool fullscreen;
	bool resizing;
	bool activated;

	uint32_t width;
	uint32_t height;

	uint32_t max_width;
	uint32_t max_height;

	uint32_t min_width;
	uint32_t min_height;
};

struct wlr_xdg_toplevel_v6 {
	struct wl_resource *resource;
	struct wlr_xdg_surface_v6 *base;
	bool added;
	struct wlr_xdg_toplevel_v6_state next; // client protocol requests
	struct wlr_xdg_toplevel_v6_state pending; // user configure requests
	struct wlr_xdg_toplevel_v6_state current;
};

// TODO split up into toplevel and popup configure
struct wlr_xdg_surface_v6_configure {
	struct wl_list link; // wlr_xdg_surface_v6::configure_list
	uint32_t serial;
	struct wlr_xdg_toplevel_v6_state *state;
};

struct wlr_xdg_surface_v6 {
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link;
	enum wlr_xdg_surface_v6_role role;
	struct wlr_xdg_toplevel_v6 *toplevel_state;

	struct wl_event_source *configure_idle;
	struct wl_list configure_list;

	char *title;
	char *app_id;

	bool has_next_geometry;
	struct wlr_box *next_geometry;
	struct wlr_box *geometry;

	struct wl_listener surface_destroy_listener;
	struct wl_listener surface_commit_listener;

	struct {
		struct wl_signal request_minimize;
		struct wl_signal commit;
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_xdg_shell_v6 *wlr_xdg_shell_v6_create(struct wl_display *display);
void wlr_xdg_shell_v6_destroy(struct wlr_xdg_shell_v6 *xdg_shell);

/**
 * Request that this toplevel surface be the given size.
 */
void wlr_xdg_toplevel_v6_set_size(struct wlr_xdg_surface_v6 *surface,
		uint32_t width, uint32_t height);

/**
 * Request that this toplevel surface show itself in an activated or deactivated
 * state.
 */
void wlr_xdg_toplevel_v6_set_activated(struct wlr_xdg_surface_v6 *surface,
		bool activated);

/**
 * Request that this toplevel surface consider itself maximized or not
 * maximized.
 */
void wlr_xdg_toplevel_v6_set_maximized(struct wlr_xdg_surface_v6 *surface,
		bool maximized);

/**
 * Request that this toplevel surface consider itself fullscreen or not
 * fullscreen.
 */
void wlr_xdg_toplevel_v6_set_fullscreen(struct wlr_xdg_surface_v6 *surface,
		bool fullscreen);

/**
 * Request that this toplevel surface consider itself to be resizing or not
 * resizing.
 */
void wlr_xdg_toplevel_v6_set_resizing(struct wlr_xdg_surface_v6 *surface,
		bool resizing);

#endif
