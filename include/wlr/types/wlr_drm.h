/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_DRM_H
#define WLR_TYPES_WLR_DRM_H

#include <wayland-server-protocol.h>

struct wlr_renderer;

/**
 * A stub implementation of Mesa's wl_drm protocol.
 *
 * It only implements the minimum necessary for modern clients to behave
 * properly. In particular, flink handles are left unimplemented.
 */
struct wlr_drm {
	struct wl_global *global;
	char *node_name;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
};

struct wlr_drm *wlr_drm_create(struct wl_display *display,
	struct wlr_renderer *renderer);

#endif
