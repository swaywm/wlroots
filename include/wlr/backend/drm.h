#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>
#include <wlr/session.h>
#include <wlr/backend.h>
#include <xf86drmMode.h> // drmModeModeInfo
#include <wlr/wayland.h>

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
	struct wlr_session *session);

void wlr_drm_output_begin(struct wlr_output *out);
void wlr_drm_output_end(struct wlr_output *out);

#endif
