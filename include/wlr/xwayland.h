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

	struct wl_event_source *sigusr1_source;
	struct wlr_xwm *xwm;
};

void wlr_xwayland_destroy(struct wlr_xwayland *wlr_xwayland);
struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
		struct wlr_compositor *compositor);

#endif
