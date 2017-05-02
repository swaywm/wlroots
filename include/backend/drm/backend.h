#ifndef DRM_BACKEND_H
#define DRM_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <libudev.h>
#include <wayland-server.h>

#include "session.h"
#include "udev.h"
#include "event.h"
#include "drm.h"

struct wlr_drm_backend {
	int fd;
	bool paused;

	struct wl_event_loop *event_loop;

	struct {
		struct wl_event_source *drm;
		struct wl_event_source *udev;
	} event_src;

	struct {
		struct wl_signal display_add;
		struct wl_signal display_rem;
		struct wl_signal display_render;
	} signals;

	size_t display_len;
	struct wlr_drm_display *displays;

	uint32_t taken_crtcs;

	struct wlr_drm_renderer renderer;
	struct wlr_session session;
	struct wlr_udev udev;
};

struct wlr_drm_backend *wlr_drm_backend_init(void);
void wlr_drm_backend_free(struct wlr_drm_backend *backend);

#endif
