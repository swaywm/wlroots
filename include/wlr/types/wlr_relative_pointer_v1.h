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


/* A global interface used for getting the relative pointer object for a given
 * pointer.
 *
 * Signals:
 * - destroy -> struct wlr_relative_pointer_manager_v1 *manager
 *   :: the manager was destroyed
 * - new_relative_pointer -> struct wlr_relative_pointer_v1 *relative_pointer
 *   :: a new relative_pointer was created
 */

struct wlr_relative_pointer_manager_v1 {
	struct wl_list resources;
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
		struct wl_signal new_relative_pointer;
	} events;

	void *data;
};


/* A wp_relative_pointer object is an extension to the wl_pointer interface
 * used for emitting relative pointer events. It shares the same focus as
 * wl_pointer objects of the same seat and will only emit events when it has
 * focus.
 *
 * Signals:
 * - destroy -> struct wlr_relative_pointer_v1 *relative_pointer
 *   :: the relative_pointer was destroyed
 */

struct wlr_relative_pointer_v1 {
	struct wl_client *client;
	struct wl_resource *resource;
	struct wl_pointer *pointer;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_relative_pointer_manager_v1 *wlr_relative_pointer_v1_create(struct wl_display *display);
void wlr_relative_pointer_v1_destroy(struct wlr_relative_pointer_manager_v1 *relative_pointer_manager);

void wlr_relative_pointer_v1_send_relative_motion(struct wl_resource *resource,
	uint64_t time, double dx, double dy, double dx_unaccel, double
	dy_unaccel);

#endif
