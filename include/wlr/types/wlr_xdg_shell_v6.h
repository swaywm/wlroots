#ifndef WLR_TYPES_WLR_XDG_SHELL_V6_H
#define WLR_TYPES_WLR_XDG_SHELL_V6_H

#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_seat.h>
#include "xdg-shell-unstable-v6-protocol.h"

struct wlr_xdg_shell_v6 {
	struct wl_global *wl_global;
	struct wl_list clients;
	struct wl_list popup_grabs;
	uint32_t ping_timeout;

	struct wl_listener display_destroy;

	struct {
		/**
		 * The `new_surface` event signals that a client has requested to
		 * create a new shell surface. At this point, the surface is ready to
		 * be configured but is not mapped or ready receive input events. The
		 * surface will be ready to be managed on the `map` event.
		 */
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

struct wlr_xdg_positioner_v6 {
	struct wlr_box anchor_rect;
	enum zxdg_positioner_v6_anchor anchor;
	enum zxdg_positioner_v6_gravity gravity;
	enum zxdg_positioner_v6_constraint_adjustment constraint_adjustment;

	struct {
		int32_t width, height;
	} size;

	struct {
		int32_t x, y;
	} offset;
};

struct wlr_xdg_popup_v6 {
	struct wlr_xdg_surface_v6 *base;
	struct wl_list link;

	struct wl_resource *resource;
	bool committed;
	struct wlr_xdg_surface_v6 *parent;
	struct wlr_seat *seat;

	// Position of the popup relative to the upper left corner of the window
	// geometry of the parent surface
	struct wlr_box geometry;

	struct wlr_xdg_positioner_v6 positioner;

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
	struct wl_listener seat_destroy;
};

enum wlr_xdg_surface_v6_role {
	WLR_XDG_SURFACE_V6_ROLE_NONE,
	WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL,
	WLR_XDG_SURFACE_V6_ROLE_POPUP,
};

struct wlr_xdg_toplevel_v6_state {
	bool maximized, fullscreen, resizing, activated;
	uint32_t width, height;
	uint32_t max_width, max_height;
	uint32_t min_width, min_height;
};

/**
 * An xdg-surface is a user interface element requiring management by the
 * compositor. An xdg-surface alone isn't useful, a role should be assigned to
 * it in order to map it.
 *
 * When a surface has a role and is ready to be displayed, the `map` event is
 * emitted. When a surface should no longer be displayed, the `unmap` event is
 * emitted. The `unmap` event is guaranteed to be emitted before the `destroy`
 * event if the view is destroyed when mapped.
 */
struct wlr_xdg_toplevel_v6 {
	struct wl_resource *resource;
	struct wlr_xdg_surface_v6 *base;
	struct wlr_xdg_surface_v6 *parent;
	bool added;

	struct wlr_xdg_toplevel_v6_state client_pending;
	struct wlr_xdg_toplevel_v6_state server_pending;
	struct wlr_xdg_toplevel_v6_state current;

	char *title;
	char *app_id;

	struct {
		struct wl_signal request_maximize;
		struct wl_signal request_fullscreen;
		struct wl_signal request_minimize;
		struct wl_signal request_move;
		struct wl_signal request_resize;
		struct wl_signal request_show_window_menu;
		struct wl_signal set_parent;
	} events;
};

struct wlr_xdg_surface_v6_configure {
	struct wl_list link; // wlr_xdg_surface_v6::configure_list
	uint32_t serial;

	struct wlr_xdg_toplevel_v6_state *toplevel_state;
};

struct wlr_xdg_surface_v6 {
	struct wlr_xdg_client_v6 *client;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link; // wlr_xdg_client_v6::surfaces
	enum wlr_xdg_surface_v6_role role;

	union {
		struct wlr_xdg_toplevel_v6 *toplevel;
		struct wlr_xdg_popup_v6 *popup;
	};

	struct wl_list popups; // wlr_xdg_popup_v6::link

	bool added, configured, mapped;
	uint32_t configure_serial;
	struct wl_event_source *configure_idle;
	uint32_t configure_next_serial;
	struct wl_list configure_list;

	bool has_next_geometry;
	struct wlr_box next_geometry;
	struct wlr_box geometry;

	struct wl_listener surface_destroy_listener;

	struct {
		struct wl_signal destroy;
		struct wl_signal ping_timeout;
		struct wl_signal new_popup;
		/**
		 * The `map` event signals that the shell surface is ready to be
		 * managed by the compositor and rendered on the screen. At this point,
		 * the surface has configured its properties, has had the opportunity
		 * to bind to the seat to receive input events, and has a buffer that
		 * is ready to be rendered. You can now safely add this surface to a
		 * list of views.
		 */
		struct wl_signal map;
		/**
		 * The `unmap` event signals that the surface is no longer in a state
		 * where it should be shown on the screen. This might happen if the
		 * surface no longer has a displayable buffer because either the
		 * surface has been hidden or is about to be destroyed.
		 */
		struct wl_signal unmap;
	} events;

	void *data;
};

struct wlr_xdg_toplevel_v6_move_event {
	struct wlr_xdg_surface_v6 *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
};

struct wlr_xdg_toplevel_v6_resize_event {
	struct wlr_xdg_surface_v6 *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
	uint32_t edges;
};

struct wlr_xdg_toplevel_v6_set_fullscreen_event {
	struct wlr_xdg_surface_v6 *surface;
	bool fullscreen;
	struct wlr_output *output;
};

struct wlr_xdg_toplevel_v6_show_window_menu_event {
	struct wlr_xdg_surface_v6 *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
	uint32_t x, y;
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
 * Request that this xdg surface closes.
 */
void wlr_xdg_surface_v6_send_close(struct wlr_xdg_surface_v6 *surface);

/**
 * Find a surface within this xdg-surface tree at the given surface-local
 * coordinates. Returns the surface and coordinates in the leaf surface
 * coordinate system or NULL if no surface is found at that location.
 */
struct wlr_surface *wlr_xdg_surface_v6_surface_at(
		struct wlr_xdg_surface_v6 *surface, double sx, double sy,
		double *sub_x, double *sub_y);

/**
 * Get the geometry for this positioner based on the anchor rect, gravity, and
 * size of this positioner.
 */
struct wlr_box wlr_xdg_positioner_v6_get_geometry(
		struct wlr_xdg_positioner_v6 *positioner);

/**
 * Get the anchor point for this popup in the toplevel parent's coordinate system.
 */
void wlr_xdg_popup_v6_get_anchor_point(struct wlr_xdg_popup_v6 *popup,
		int *toplevel_sx, int *toplevel_sy);

/**
 * Convert the given coordinates in the popup coordinate system to the toplevel
 * surface coordinate system.
 */
void wlr_xdg_popup_v6_get_toplevel_coords(struct wlr_xdg_popup_v6 *popup,
		int popup_sx, int popup_sy, int *toplevel_sx, int *toplevel_sy);

/**
 * Set the geometry of this popup to unconstrain it according to its
 * xdg-positioner rules. The box should be in the popup's root toplevel parent
 * surface coordinate system.
 */
void wlr_xdg_popup_v6_unconstrain_from_box(struct wlr_xdg_popup_v6 *popup,
		struct wlr_box *toplevel_sx_box);

/**
  Invert the right/left anchor and gravity for this positioner. This can be
  used to "flip" the positioner around the anchor rect in the x direction.
 */
void wlr_positioner_v6_invert_x(
		struct wlr_xdg_positioner_v6 *positioner);

/**
  Invert the top/bottom anchor and gravity for this positioner. This can be
  used to "flip" the positioner around the anchor rect in the y direction.
 */
void wlr_positioner_v6_invert_y(
		struct wlr_xdg_positioner_v6 *positioner);

bool wlr_surface_is_xdg_surface_v6(struct wlr_surface *surface);

struct wlr_xdg_surface_v6 *wlr_xdg_surface_v6_from_wlr_surface(
		struct wlr_surface *surface);

/**
 * Get the surface geometry.
 * This is either the geometry as set by the client, or defaulted to the bounds
 * of the surface + the subsurfaces (as specified by the protocol).
 *
 * The x and y value can be <0
 */
void wlr_xdg_surface_v6_get_geometry(struct wlr_xdg_surface_v6 *surface, struct wlr_box *box);

/**
 * Call `iterator` on each surface in the xdg-surface tree, with the surface's
 * position relative to the root xdg-surface. The function is called from root to
 * leaves (in rendering order).
 */
void wlr_xdg_surface_v6_for_each_surface(struct wlr_xdg_surface_v6 *surface,
	wlr_surface_iterator_func_t iterator, void *user_data);

#endif
