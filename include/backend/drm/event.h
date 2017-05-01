#ifndef EVENT_H
#define EVENT_H

#include <stdbool.h>
#include "drm.h"

enum wlr_drm_event_type {
	DRM_EV_NONE,
	DRM_EV_RENDER,
	DRM_EV_DISPLAY_REM,
	DRM_EV_DISPLAY_ADD,
};

struct wlr_drm_event {
	enum wlr_drm_event_type type;
	struct wlr_drm_display *display;
};

struct wlr_drm_backend;
bool wlr_drm_get_event(struct wlr_drm_backend *backend,
		struct wlr_drm_event *restrict ret);
bool wlr_drm_add_event(struct wlr_drm_backend *backend,
		struct wlr_drm_display *disp, enum wlr_drm_event_type type);

#endif
