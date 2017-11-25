#ifndef WLR_TYPES_WLR_XDG_SHELL_V6_H
#define WLR_TYPES_WLR_XDG_SHELL_V6_H

#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_seat.h>
#include <wayland-server.h>

struct wlr_xdg_shell_v6 {
	struct wl_global *wl_global;
	struct wl_list clients;
	struct wl_list popup_grabs;
	uint32_t ping_timeout;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_xdg_client_v6 {
	struct wlr_xdg_shell_v6 *shell;
	struct wl_resource *resource;
	struct wl_client *client;
	struct wl_list surfaces;

	struct wl_list link; // wlr_xdg_shell_v6::clients

	uint32_t ping_serial;
	struct wl_event_source *ping_timer;
};

struct wlr_xdg_popup_v6 {
	struct wlr_xdg_surface_v6 *base;

	struct wl_resource *resource;
	bool committed;
	struct wlr_xdg_surface_v6 *parent;
	struct wlr_seat *seat;
	struct wlr_box geometry;

	struct wl_list grab_link; // wlr_xdg_popup_grab_v6::popups
};

// each seat gets a popup grab
struct wlr_xdg_popup_grab_v6 {
	struct wl_client *client;
	struct wlr_seat_pointer_grab pointer_grab;
	struct wlr_seat_keyboard_grab keyboard_grab;
	struct wlr_seat *seat;
	struct wl_list popups;
	struct wl_list link; // wlr_xdg_shell_v6::popup_grabs
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
	struct wlr_xdg_surface_v6 *parent;
	bool added;
	struct wlr_xdg_toplevel_v6_state next; // client protocol requests
	struct wlr_xdg_toplevel_v6_state pending; // user configure requests
	struct wlr_xdg_toplevel_v6_state current;
};

struct wlr_xdg_surface_v6_configure {
	struct wl_list link; // wlr_xdg_surface_v6::configure_list
	uint32_t serial;
	struct wlr_xdg_toplevel_v6_state state;
};

struct wlr_xdg_surface_v6 {
	struct wlr_xdg_client_v6 *client;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link; // wlr_xdg_client_v6::surfaces
	enum wlr_xdg_surface_v6_role role;

	union {
		struct wlr_xdg_toplevel_v6 *toplevel_state;
		struct wlr_xdg_popup_v6 *popup_state;
	};

	struct wl_list popups;
	struct wl_list popup_link;

	bool configured;
	bool added;
	uint32_t configure_serial;
	struct wl_event_source *configure_idle;
	uint32_t configure_next_serial;
	struct wl_list configure_list;

	char *title;
	char *app_id;

	bool has_next_geometry;
	struct wlr_box *next_geometry;
	struct wlr_box *geometry;

	struct wl_listener surface_destroy_listener;
	struct wl_listener surface_commit_listener;

	struct {
		struct wl_signal commit;
		struct wl_signal destroy;
		struct wl_signal ping_timeout;
		struct wl_signal ack_configure;

		struct wl_signal request_maximize;
		struct wl_signal request_fullscreen;
		struct wl_signal request_minimize;
		struct wl_signal request_move;
		struct wl_signal request_resize;
		struct wl_signal request_show_window_menu;
	} events;

	void *data;
};

struct wlr_xdg_toplevel_v6_move_event {
	struct wl_client *client;
	struct wlr_xdg_surface_v6 *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
};

struct wlr_xdg_toplevel_v6_resize_event {
	struct wl_client *client;
	struct wlr_xdg_surface_v6 *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
	uint32_t edges;
};

struct wlr_xdg_toplevel_v6_show_window_menu_event {
	struct wl_client *client;
	struct wlr_xdg_surface_v6 *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
	uint32_t x;
	uint32_t y;
};

struct wlr_xdg_shell_v6 *wlr_xdg_shell_v6_create(struct wl_display *display);
void wlr_xdg_shell_v6_destroy(struct wlr_xdg_shell_v6 *xdg_shell);

/**
 * Send a ping to the surface. If the surface does not respond in a reasonable
 * amount of time, the ping_timeout event will be emitted.
 */
void wlr_xdg_surface_v6_ping(struct wlr_xdg_surface_v6 *surface);

/**
 * Request that this toplevel surface be the given size. Returns the associated
 * configure serial.
 */
uint32_t wlr_xdg_toplevel_v6_set_size(struct wlr_xdg_surface_v6 *surface,
		uint32_t width, uint32_t height);

/**
 * Request that this toplevel surface show itself in an activated or deactivated
 * state. Returns the associated configure serial.
 */
uint32_t wlr_xdg_toplevel_v6_set_activated(struct wlr_xdg_surface_v6 *surface,
		bool activated);

/**
 * Request that this toplevel surface consider itself maximized or not
 * maximized. Returns the associated configure serial.
 */
uint32_t wlr_xdg_toplevel_v6_set_maximized(struct wlr_xdg_surface_v6 *surface,
		bool maximized);

/**
 * Request that this toplevel surface consider itself fullscreen or not
 * fullscreen. Returns the associated configure serial.
 */
uint32_t wlr_xdg_toplevel_v6_set_fullscreen(struct wlr_xdg_surface_v6 *surface,
		bool fullscreen);

/**
 * Request that this toplevel surface consider itself to be resizing or not
 * resizing. Returns the associated configure serial.
 */
uint32_t wlr_xdg_toplevel_v6_set_resizing(struct wlr_xdg_surface_v6 *surface,
		bool resizing);

/**
 * Request that this toplevel surface closes.
 */
void wlr_xdg_toplevel_v6_send_close(struct wlr_xdg_surface_v6 *surface);

/**
 * Compute the popup position in surface-local coordinates.
 */
void wlr_xdg_surface_v6_popup_get_position(struct wlr_xdg_surface_v6 *surface,
		double *popup_sx, double *popup_sy);

/**
 * Find a popup within this surface at the surface-local coordinates. Returns
 * the popup and coordinates in the topmost surface coordinate system or NULL if
 * no popup is found at that location.
 */
struct wlr_xdg_surface_v6 *wlr_xdg_surface_v6_popup_at(
		struct wlr_xdg_surface_v6 *surface, double sx, double sy,
		double *popup_sx, double *popup_sy);

#endif
