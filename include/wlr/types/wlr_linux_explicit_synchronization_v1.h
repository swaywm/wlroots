/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_LINUX_EXPLICIT_SYNCHRONIZATION_V1_H
#define WLR_TYPES_WLR_LINUX_EXPLICIT_SYNCHRONIZATION_V1_H

#include <wayland-server-core.h>
#include <wlr/util/addon.h>

struct wlr_linux_surface_synchronization_v1_state {
	int acquire_fence_fd;
	struct wlr_linux_buffer_release_v1 *buffer_release;
};

struct wlr_linux_surface_synchronization_v1 {
	struct wl_resource *resource;
	struct wlr_surface *surface;

	// private state

	struct wlr_addon addon;

	struct wlr_linux_surface_synchronization_v1_state pending, current;

	struct wl_listener surface_commit;
};

struct wlr_linux_buffer_release_v1 {
	struct wl_resource *resource;
};

struct wlr_linux_explicit_synchronization_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
	} events;

	// private state

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

/**
 * Signal the provided timeline synchronization point when the last submitted
 * buffer is ready to be acquired.
 */
bool wlr_linux_explicit_synchronization_v1_signal_surface_timeline(
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync,
	struct wlr_surface *surface, struct wlr_render_timeline *timeline,
	uint64_t dst_point);

/**
 * Send a timeline synchronization point to the client which can be used to
 * wait for the buffer to be released.
 *
 * The synchronization point must already be materialized: wait-before-submit
 * is not supported.
 */
bool wlr_linux_explicit_synchronization_v1_wait_surface_timeline(
	struct wlr_linux_explicit_synchronization_v1 *explicit_sync,
	struct wlr_surface *surface, struct wlr_render_timeline *timeline,
	uint64_t src_point);

#endif
