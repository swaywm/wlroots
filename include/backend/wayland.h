#ifndef BACKEND_WAYLAND_H
#define BACKEND_WAYLAND_H

#include <stdbool.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend/wayland.h>
#include <wlr/render.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_box.h>

struct wlr_wl_backend {
	struct wlr_backend backend;

	/* local state */
	bool started;
	struct wl_display *local_display;
	struct wl_list devices;
	struct wl_list outputs;
	struct wlr_egl egl;
	struct wlr_renderer *renderer;
	size_t requested_outputs;
	struct wl_listener local_display_destroy;
	/* remote state */
	struct wl_display *remote_display;
	struct wl_event_source *remote_display_src;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct zxdg_shell_v6 *shell;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	char *seat_name;
};

struct wlr_wl_backend_output {
	struct wlr_output wlr_output;

	struct wlr_wl_backend *backend;
	struct wl_surface *surface;
	struct zxdg_surface_v6 *xdg_surface;
	struct zxdg_toplevel_v6 *xdg_toplevel;
	struct wl_egl_window *egl_window;
	struct wl_callback *frame_callback;

	struct {
		struct wl_shm_pool *pool;
		void *buffer; // actually a (client-side) struct wl_buffer*
		uint32_t buf_size;
		uint8_t *data;
		struct wl_surface *surface;
		int32_t hotspot_x, hotspot_y;
	} cursor;

	uint32_t enter_serial;

	void *egl_surface;
	struct wl_list link;
};

struct wlr_wl_input_device {
	struct wlr_input_device wlr_input_device;

	struct wlr_wl_backend *backend;
	void *resource;
};

struct wlr_wl_pointer {
	struct wlr_pointer wlr_pointer;
	enum wlr_axis_source axis_source;
	struct wlr_wl_backend_output *current_output;
};

void wlr_wl_registry_poll(struct wlr_wl_backend *backend);
void wlr_wl_output_update_cursor(struct wlr_wl_backend_output *output);
struct wlr_wl_backend_output *wlr_wl_output_for_surface(
		struct wlr_wl_backend *backend, struct wl_surface *surface);
void wlr_wl_output_layout_get_box(struct wlr_wl_backend *backend,
		struct wlr_box *box);

extern const struct wl_seat_listener seat_listener;

#endif
