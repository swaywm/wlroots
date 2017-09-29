#ifndef WLR_TYPES_WLR_WL_SHELL_H
#define WLR_TYPES_WLR_WL_SHELL_H

#include <stdbool.h>
#include <wayland-server.h>

struct wlr_wl_shell {
	struct wl_global *wl_global;
	struct wl_list wl_resources;
	struct wl_list surfaces;
	uint32_t ping_timeout;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_wl_shell_surface_transient_state {
	struct wlr_wl_shell_surface *parent;
	int32_t x;
	int32_t y;
	enum wl_shell_surface_transient flags;
};

struct wlr_wl_shell_surface_popup_state {
	struct wlr_seat_handle *seat_handle;
	uint32_t serial;
};

enum wlr_wl_shell_surface_state {
	WLR_WL_SHELL_SURFACE_STATE_NONE,
	WLR_WL_SHELL_SURFACE_STATE_TOPLEVEL,
	WLR_WL_SHELL_SURFACE_STATE_TRANSIENT,
	WLR_WL_SHELL_SURFACE_STATE_POPUP,
};

struct wlr_wl_shell_surface {
	struct wlr_wl_shell *shell;
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_list link;

	uint32_t ping_serial;
	struct wl_event_source *ping_timer;

	enum wlr_wl_shell_surface_state state;
	struct wlr_wl_shell_surface_transient_state *transient_state;
	struct wlr_wl_shell_surface_popup_state *popup_state;

	char *title;
	char *class;

	struct wl_listener surface_destroy_listener;

	struct {
		struct wl_signal destroy;
		struct wl_signal ping_timeout;

		struct wl_signal request_move;
		struct wl_signal request_resize;
		struct wl_signal request_set_fullscreen;
		struct wl_signal request_set_maximized;

		struct wl_signal set_state;
		struct wl_signal set_title;
		struct wl_signal set_class;
	} events;

	void *data;
};

struct wlr_wl_shell_surface_move_event {
	struct wl_client *client;
	struct wlr_wl_shell_surface *surface;
	struct wlr_seat_handle *seat_handle;
	uint32_t serial;
};

struct wlr_wl_shell_surface_resize_event {
	struct wl_client *client;
	struct wlr_wl_shell_surface *surface;
	struct wlr_seat_handle *seat_handle;
	uint32_t serial;
	enum wl_shell_surface_resize edges;
};

struct wlr_wl_shell_surface_set_fullscreen_event {
	struct wl_client *client;
	struct wlr_wl_shell_surface *surface;
	enum wl_shell_surface_fullscreen_method method;
	uint32_t framerate;
	struct wlr_output *output;
};

struct wlr_wl_shell_surface_set_maximized_event {
	struct wl_client *client;
	struct wlr_wl_shell_surface *surface;
	struct wlr_output *output;
};

struct wlr_wl_shell *wlr_wl_shell_create(struct wl_display *display);
void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell);

void wlr_wl_shell_surface_ping(struct wlr_wl_shell_surface *surface);
void wlr_wl_shell_surface_configure(struct wlr_wl_shell_surface *surface,
	enum wl_shell_surface_resize edges, int32_t width, int32_t height);
void wlr_wl_shell_surface_popup_done(struct wlr_wl_shell_surface *surface);

#endif
