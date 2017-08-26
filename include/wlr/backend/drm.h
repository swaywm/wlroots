#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>
#include <wlr/backend/session.h>
#include <wlr/backend.h>

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
		struct wlr_session *session, int gpu_fd);

bool wlr_backend_is_drm(struct wlr_backend *backend);

#endif
