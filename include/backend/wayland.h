#ifndef BACKEND_WAYLAND_H
#define BACKEND_WAYLAND_H

#include <stdbool.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/backend/wayland.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_box.h>
#include <wlr/render/drm_format_set.h>

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
	size_t last_output_num;
	struct wl_listener local_display_destroy;
	/* remote state */
	struct wl_display *remote_display;
	struct wl_event_source *remote_display_src;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct xdg_wm_base *xdg_wm_base;
	struct zxdg_decoration_manager_v1 *zxdg_decoration_manager_v1;
	struct zwp_pointer_gestures_v1 *zwp_pointer_gestures_v1;
	struct wp_presentation *presentation;
	struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1;
	struct zwp_relative_pointer_manager_v1 *zwp_relative_pointer_manager_v1;
	struct wlr_wl_seat *seat;
	struct wlr_wl_pointer *current_pointer;
	struct zwp_tablet_manager_v2 *tablet_manager;
	struct wlr_drm_format_set linux_dmabuf_v1_formats;
};

struct wlr_wl_buffer {
	struct wlr_buffer *buffer;
	struct wl_buffer *wl_buffer;
};

struct wlr_wl_presentation_feedback {
	struct wlr_wl_output *output;
	struct wl_list link;
	struct wp_presentation_feedback *feedback;
	uint32_t commit_seq;
};

struct wlr_wl_output {
	struct wlr_output wlr_output;

	struct wlr_wl_backend *backend;
	struct wl_list link;

	struct wl_surface *surface;
	struct wl_callback *frame_callback;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zxdg_toplevel_decoration_v1 *zxdg_toplevel_decoration_v1;
	struct wl_egl_window *egl_window;
	EGLSurface egl_surface;
	struct wl_list presentation_feedbacks;

	uint32_t enter_serial;

	struct {
		struct wl_surface *surface;
		struct wl_egl_window *egl_window;
		int32_t hotspot_x, hotspot_y;
		int32_t width, height;
	} cursor;
};

struct wlr_wl_input_device {
	struct wlr_input_device wlr_input_device;
	uint32_t fingers;

	struct wlr_wl_backend *backend;
	void *resource;
};

struct wlr_wl_pointer {
	struct wlr_pointer wlr_pointer;

	struct wlr_wl_input_device *input_device;
	struct wl_pointer *wl_pointer;
	struct zwp_pointer_gesture_swipe_v1 *gesture_swipe;
	struct zwp_pointer_gesture_pinch_v1 *gesture_pinch;
	struct zwp_relative_pointer_v1 *relative_pointer;
	enum wlr_axis_source axis_source;
	int32_t axis_discrete;
	struct wlr_wl_output *output;

	struct wl_listener output_destroy;
};

struct wlr_wl_seat {
	struct wl_seat *wl_seat;

	char *name;
	struct wl_touch *touch;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
};

struct wlr_wl_backend *get_wl_backend_from_backend(struct wlr_backend *backend);
void update_wl_output_cursor(struct wlr_wl_output *output);
struct wlr_wl_pointer *pointer_get_wl(struct wlr_pointer *wlr_pointer);
void create_wl_pointer(struct wl_pointer *wl_pointer, struct wlr_wl_output *output);
void create_wl_keyboard(struct wl_keyboard *wl_keyboard, struct wlr_wl_backend *wl);
void create_wl_touch(struct wl_touch *wl_touch, struct wlr_wl_backend *wl);
struct wlr_wl_input_device *create_wl_input_device(
	struct wlr_wl_backend *backend, enum wlr_input_device_type type);
void create_wl_seat(struct wl_seat *wl_seat, struct wlr_wl_backend *wl);
void destroy_wl_seats(struct wlr_wl_backend *wl);

extern const struct wl_seat_listener seat_listener;

struct wlr_wl_tablet_seat *wl_add_tablet_seat(
		struct zwp_tablet_manager_v2 *manager,
		struct wl_seat *seat, struct wlr_wl_backend *backend);

#endif
