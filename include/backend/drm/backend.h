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

#include "udev.h"
#include "event.h"
#include "drm.h"

struct wlr_drm_backend {
	int fd;

	struct wl_event_source *drm_event;

	struct {
		struct wl_signal output_add;
		struct wl_signal output_rem;
		struct wl_signal output_render;
	} signals;

	uint32_t taken_crtcs;
	list_t *outputs;

	struct wlr_drm_renderer renderer;
	struct wlr_session *session;
	struct wlr_udev udev;
};

#endif
