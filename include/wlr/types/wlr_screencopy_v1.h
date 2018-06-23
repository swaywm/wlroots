#ifndef WLR_TYPES_WLR_SCREENCOPY_V1_H
#define WLR_TYPES_WLR_SCREENCOPY_V1_H

#include <wayland-server.h>

struct wlr_screencopy_manager_v1 {
	struct wl_global *global;
	struct wl_list resources; // wl_resource
	struct wl_list frames; // wlr_screencopy_frame_v1::link

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_screencopy_frame_v1 {
	struct wl_resource *resource;
	struct wlr_screencopy_manager_v1 *manager;
	struct wl_list link;

	struct wlr_box buffer_box;

	struct wl_shm_buffer *buffer;

	struct wlr_output *output;
	struct wl_listener output_swap_buffers;

	void *data;
};

struct wlr_screencopy_manager_v1 *wlr_screencopy_manager_v1_create(
	struct wl_display *display);
void wlr_screencopy_manager_v1_destroy(
	struct wlr_screencopy_manager_v1 *screencopy);

#endif
