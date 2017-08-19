#ifndef _WLR_XWAYLAND_H
#define _WLR_XWAYLAND_H
#include <time.h>
#include <sys/types.h>
#include <stdbool.h>
#include <wlr/types/wlr_compositor.h>

struct wlr_xwm;

struct wlr_xwayland {
        pid_t pid;
        int display;
        int x_fd[2], wl_fd[2], wm_fd[2];
        struct wl_client *client;
	struct wl_display *wl_display;
	struct wlr_compositor *compositor;
	time_t server_start;

	struct wlr_xwm *xwm;
};

void wlr_xwayland_finish(struct wlr_xwayland *wlr_xwayland);
bool wlr_xwayland_init(struct wlr_xwayland *wlr_xwayland,
		struct wl_display *wl_display, struct wlr_compositor *compositor);

#endif
