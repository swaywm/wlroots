#ifndef BACKEND_MULTI_H
#define BACKEND_MULTI_H

#include <wayland-util.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>

struct wlr_multi_backend_gpu_hotplug {
	struct wl_listener add_gpu_signal;
	struct wl_listener remove_gpu_signal;
	struct wlr_backend *primary_drm;
};

struct wlr_multi_backend {
	struct wlr_backend backend;
	struct wlr_session *session;

	struct wl_list backends;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal backend_add;
		struct wl_signal backend_remove;
	} events;

	struct wlr_multi_backend_gpu_hotplug hotplug;
};

void wlr_multi_backend_init_gpu_hotplug(struct wlr_multi_backend *backend, struct wlr_backend *primary_drm);

#endif
