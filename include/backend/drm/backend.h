#ifndef DRM_BACKEND_H
#define DRM_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <libudev.h>
#include <wayland-server.h>

#include <wlr/session.h>
#include <wlr/common/list.h>
#include <wlr/backend/drm.h>

#include "backend.h"
#include "udev.h"
#include "event.h"
#include "drm.h"

struct wlr_backend_state {
	int fd;

	struct wlr_backend *backend;
	struct wl_event_source *drm_event;

	struct wl_listener device_paused;
	struct wl_listener device_resumed;

	uint32_t taken_crtcs;
	list_t *outputs;

	struct wlr_drm_renderer renderer;
	struct wlr_session *session;
	struct wlr_udev udev;
};

#endif
