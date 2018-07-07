#ifndef WLR_TYPES_WLR_WL_SHELL_H
#define WLR_TYPES_WLR_WL_SHELL_H

#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

struct wlr_wl_shell {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;
	struct wl_list popup_grabs;
	uint32_t ping_timeout;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_wl_shell_surface_transient_state {
	int32_t x;
	int32_t y;
	enum wl_shell_surface_transient flags;
};

struct wlr_wl_shell_surface_popup_state {
	struct wlr_seat *seat;
	uint32_t serial;
};

// each seat gets a popup grab
struct wlr_wl_shell_popup_grab {
	struct wl_client *client;
	struct wlr_seat_pointer_grab pointer_grab;
	struct wlr_seat *seat;
	struct wl_list popups;
	struct wl_list link; // wlr_wl_shell::popup_grabs
};

enum wlr_wl_shell_surface_state {
	WLR_WL_SHELL_SURFACE_STATE_NONE,
	WLR_WL_SHELL_SURFACE_STATE_TOPLEVEL,
	WLR_WL_SHELL_SURFACE_STATE_MAXIMIZED,
	WLR_WL_SHELL_SURFACE_STATE_FULLSCREEN,
	WLR_WL_SHELL_SURFACE_STATE_TRANSIENT,
	WLR_WL_SHELL_SURFACE_STATE_POPUP,
};

struct wlr_wl_shell_surface {
	struct wlr_wl_shell *shell;
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	bool configured;
	struct wl_list link; // wlr_wl_shell::surfaces

	uint32_t ping_serial;
	struct wl_event_source *ping_timer;

	enum wlr_wl_shell_surface_state state;
	struct wlr_wl_shell_surface_transient_state *transient_state;
	struct wlr_wl_shell_surface_popup_state *popup_state;
	struct wl_list grab_link; // wlr_wl_shell_popup_grab::popups

	char *title;
	char *class;

	struct wl_listener surface_destroy;

	struct wlr_wl_shell_surface *parent;
	struct wl_list popup_link;
	struct wl_list popups;
	bool popup_mapped;

	struct {
		struct wl_signal destroy;
		struct wl_signal ping_timeout;
		struct wl_signal new_popup;

		struct wl_signal request_move;
		struct wl_signal request_resize;
		struct wl_signal request_fullscreen;
		struct wl_signal request_maximize;

		struct wl_signal set_state;
		struct wl_signal set_title;
		struct wl_signal set_class;
	} events;

	void *data;
};

struct wlr_wl_shell_surface_move_event {
	struct wlr_wl_shell_surface *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
};

struct wlr_wl_shell_surface_resize_event {
	struct wlr_wl_shell_surface *surface;
	struct wlr_seat_client *seat;
	uint32_t serial;
	enum wl_shell_surface_resize edges;
};

struct wlr_wl_shell_surface_set_fullscreen_event {
	struct wlr_wl_shell_surface *surface;
	enum wl_shell_surface_fullscreen_method method;
	uint32_t framerate;
	struct wlr_output *output;
};

struct wlr_wl_shell_surface_maximize_event {
	struct wlr_wl_shell_surface *surface;
	struct wlr_output *output;
};

/**
 * Create a wl_shell for this display.
 */
struct wlr_wl_shell *wlr_wl_shell_create(struct wl_display *display);

/**
 * Destroy this surface.
 */
void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell);

/**
 * Send a ping to the surface. If the surface does not respond with a pong
 * within a reasonable amount of time, the ping timeout event will be emitted.
 */
void wlr_wl_shell_surface_ping(struct wlr_wl_shell_surface *surface);

/**
 * Request that the surface configure itself to be the given size.
 */
void wlr_wl_shell_surface_configure(struct wlr_wl_shell_surface *surface,
	enum wl_shell_surface_resize edges, int32_t width, int32_t height);

/**
 * Find a surface within this wl-shell surface tree at the given surface-local
 * coordinates. Returns the surface and coordinates in the leaf surface
 * coordinate system or NULL if no surface is found at that location.
 */
struct wlr_surface *wlr_wl_shell_surface_surface_at(
		struct wlr_wl_shell_surface *surface, double sx, double sy,
		double *sub_sx, double *sub_sy);

bool wlr_surface_is_wl_shell_surface(struct wlr_surface *surface);

struct wlr_wl_shell_surface *wlr_wl_shell_surface_from_wlr_surface(
		struct wlr_surface *surface);

/**
 * Call `iterator` on each surface in the shell surface tree, with the surface's
 * position relative to the root xdg-surface. The function is called from root to
 * leaves (in rendering order).
 */
void wlr_wl_shell_surface_for_each_surface(struct wlr_wl_shell_surface *surface,
	wlr_surface_iterator_func_t iterator, void *user_data);

#endif
