/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_EXPORT_DMABUF_V1_H
#define WLR_TYPES_WLR_EXPORT_DMABUF_V1_H

#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/render/dmabuf.h>

struct wlr_export_dmabuf_manager_v1 {
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link
	struct wl_list frames; // wlr_export_dmabuf_frame_v1::link

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_export_dmabuf_frame_v1 {
	struct wl_resource *resource;
	struct wlr_export_dmabuf_manager_v1 *manager;
	struct wl_list link; // wlr_export_dmabuf_manager_v1::frames

	struct wlr_dmabuf_attributes attribs;
	struct wlr_output *output;

	bool cursor_locked;

	struct wl_listener output_swap_buffers;
};

struct wlr_export_dmabuf_manager_v1 *wlr_export_dmabuf_manager_v1_create(
	struct wl_display *display);
void wlr_export_dmabuf_manager_v1_destroy(
	struct wlr_export_dmabuf_manager_v1 *manager);

#endif
