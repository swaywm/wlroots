#ifndef _WLR_XWAYLAND_H
#define _WLR_XWAYLAND_H
#include <time.h>
#include <stdbool.h>
#include <wlr/types/wlr_compositor.h>
#include <xcb/xcb.h>

struct wlr_xwm;

struct wlr_xwayland {
	pid_t pid;
	int display;
	int x_fd[2], wl_fd[2], wm_fd[2];
	struct wl_client *client;
	struct wl_display *wl_display;
	struct wlr_compositor *compositor;
	time_t server_start;

	struct wl_event_source *sigusr1_source;
	struct wl_listener destroy_listener;
	struct wlr_xwm *xwm;
	struct wl_list displayable_windows;
};

struct wlr_x11_window {
	xcb_window_t window_id;
	uint32_t surface_id;
	struct wl_list link;

	struct wl_resource *surface;
	struct wl_listener surface_destroy_listener;
	int16_t x, y;
	uint16_t width, height;
	bool override_redirect;
};

void wlr_xwayland_destroy(struct wlr_xwayland *wlr_xwayland);
struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
		struct wlr_compositor *compositor);

#endif
