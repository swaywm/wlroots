#ifndef _WLR_INTERNAL_BACKEND_WAYLAND_H
#define _WLR_INTERNAL_BACKEND_WAYLAND_H

#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-egl.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/list.h>
#include "backend/egl.h"

struct wlr_backend_state {
	/* local state */
	struct wl_display *local_display;
	struct wlr_backend *backend;
	list_t *devices;
	list_t *outputs;
	struct wlr_egl egl;
	size_t requested_outputs;
	/* remote state */
	struct wl_display *remote_display;
	struct wl_event_source *remote_display_src;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
	struct wl_seat *seat;
	const char *seatName;
};

struct wlr_output_state {
	struct wlr_backend_state *backend;
	struct wlr_output *wlr_output;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	struct wl_egl_window* egl_window;
	struct wl_callback* frame_callback;
	void *egl_surface;
};

struct wlr_input_device_state {
	struct wlr_backend_state* backend;
	void *resource;
};

struct wlr_pointer_state {
	enum wlr_axis_source axis_source;
	struct wlr_output *current_output;
};

void wlr_wl_registry_poll(struct wlr_backend_state *backend);
struct wlr_output *wlr_wl_output_for_surface(struct wlr_backend_state *backend,
	struct wl_surface *surface);

extern const struct wl_seat_listener seat_listener;

#endif
