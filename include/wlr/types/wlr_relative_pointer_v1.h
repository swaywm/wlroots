/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_RELATIVE_POINTER_V1_H
#define WLR_TYPES_WLR_RELATIVE_POINTER_V1_H

#include <wayland-server.h>


/* This protocol specifies a set of interfaces used for making clients able to
 * receive relative pointer events not obstructed by barriers (such as the
 * monitor edge or other pointer barriers).
 */

struct wlr_relative_pointer_manager_v1 {
	struct wl_list resources;
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
		struct wl_signal get_relative_pointer;
	} requests;

	void *data;
};

struct wlr_relative_pointer_v1 {
	struct wl_resource *resource;

	struct {
		struct wl_signal destroy;
	} destroy;

	void *data;
};

struct wlr_relative_pointer_manager_v1 *wlr_relative_pointer_v1_create(struct wl_display *display);
void wlr_relative_pointer_v1_destroy(struct wlr_relative_pointer_manager_v1 *relative_pointer_manager);

#endif
