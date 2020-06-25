/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_LINUX_EXPLICIT_SYNCHRONIZATION_H
#define WLR_TYPES_WLR_LINUX_EXPLICIT_SYNCHRONIZATION_H

#include <wayland-server-core.h>

struct wlr_linux_surface_synchronization_v1 {
	struct wl_resource *resource;
	struct wlr_surface *surface;

	int pending_fence_fd;
	struct wlr_linux_buffer_release_v1 *pending_buffer_release;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
};

struct wlr_linux_buffer_release_v1 {
	struct wl_resource *resource;

	struct wlr_buffer *buffer;

	struct wl_listener buffer_destroy;
	struct wl_listener buffer_release;
};

struct wlr_linux_explicit_synchronization_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
};

/**
 * Advertise explicit synchronization support to clients.
 *
 * The compositor must be prepared to handle fences coming from clients and to
 * send release fences correctly. In particular, both the renderer and the
 * backend need to support explicit synchronization.
 */
struct wlr_linux_explicit_synchronization_v1 *
wlr_linux_explicit_synchronization_v1_create(struct wl_display *display);

#endif
