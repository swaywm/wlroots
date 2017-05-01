#ifndef DRM_BACKEND_H
#define DRM_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <libudev.h>

#include "session.h"
#include "udev.h"
#include "event.h"
#include "drm.h"

struct wlr_drm_backend {
	int fd;
	bool paused;

	// Priority Queue (Max-heap)
	size_t event_cap;
	size_t event_len;
	struct wlr_drm_event *events;

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
