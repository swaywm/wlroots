#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>
#include <wlr/session.h>
#include <wlr/backend.h>

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
	struct wlr_session *session);

#endif
