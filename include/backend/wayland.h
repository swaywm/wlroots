#ifndef _WLR_INTERNAL_BACKEND_WAYLAND_H
#define _WLR_INTERNAL_BACKEND_WAYLAND_H

#include <wayland-client.h>
#include <wayland-server.h>
#include <wlr/common/list.h>
#include <wlr/wayland.h>
#include <wlr/backend/wayland.h>

struct wlr_backend_state {
	/* local state */
	struct wl_display *local_display;
	/* remote state */
	struct wl_display *remote_display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
	struct wlr_wl_seat *seat;
	list_t *outputs;
};

void wlr_wlb_registry_poll(struct wlr_backend_state *backend);

extern const struct wl_seat_listener seat_listener;
extern const struct wl_output_listener output_listener;

#endif
