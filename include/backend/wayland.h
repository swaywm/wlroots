#ifndef _WLR_INTERNAL_BACKEND_WAYLAND_H
#define _WLR_INTERNAL_BACKEND_WAYLAND_H

#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-egl.h>
#include <wlr/common/list.h>
#include <wlr/backend/wayland.h>
#include "backend/egl.h"

struct wlr_backend_state {
	/* local state */
	struct wl_display *local_display;
	/* remote state */
	struct wl_display *remote_display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shell *shell;
	struct wl_shm *shm;
	struct wl_seat *seat;
	const char *seatName;

	struct wlr_backend *backend;
	list_t *devices;

	struct wl_event_source* remote_display_src;

	size_t num_outputs;
	struct wlr_output **outputs;
	struct wlr_egl egl;
};

struct wlr_output_state {
	size_t id;
	struct wlr_backend_state *backend;
	struct wlr_output *output;
	struct wl_surface *surface;
	struct wl_shell_surface *shell_surface;
	struct wl_egl_window* egl_window;
	struct wl_callback* frame_callback;
	void* egl_surface;
};

struct wlr_input_device_state {
	enum wlr_input_device_type type;
	void *resource;
};

void wlr_wl_registry_poll(struct wlr_backend_state *backend);
struct wlr_output *wlr_wl_output_create(struct wlr_backend_state* backend,
		size_t id);

extern const struct wl_seat_listener seat_listener;

#endif
