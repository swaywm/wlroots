#ifndef WLR_TYPES_WLR_EXPORT_DMABUF_V1_H
#define WLR_TYPES_WLR_EXPORT_DMABUF_V1_H

#include <wayland-server.h>
#include <wlr/render/dmabuf.h>

struct wlr_export_dmabuf_manager_v1;

struct wlr_export_dmabuf_frame_v1 {
	struct wl_resource *resource;
	struct wlr_export_dmabuf_manager_v1 *manager;
	struct wl_list link;

	struct wlr_dmabuf_attributes attribs;
	struct wlr_output *output;

	struct wl_listener output_swap_buffers;
};

struct wlr_export_dmabuf_manager_v1 {
	struct wl_global *global;
	struct wl_list resources;
	struct wl_list frames;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_export_dmabuf_manager_v1 *wlr_export_dmabuf_manager_v1_create(
	struct wl_display *display);
void wlr_export_dmabuf_manager_v1_destroy(
	struct wlr_export_dmabuf_manager_v1 *manager);

#endif
