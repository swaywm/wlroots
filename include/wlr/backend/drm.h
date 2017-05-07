#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>
#include <wlr/session.h>
#include <wlr/backend.h>
#include <xf86drmMode.h> // drmModeModeInfo

struct wlr_drm_backend;
struct wlr_drm_output;

struct wlr_drm_mode {
	uint16_t width;
	uint16_t height;
	uint32_t rate;
	drmModeModeInfo mode;
};

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
	struct wlr_session *session);

const char *wlr_drm_output_get_name(struct wlr_drm_output *out);

struct wlr_drm_mode *wlr_drm_output_get_modes(struct wlr_drm_output *out, size_t *count);
bool wlr_drm_output_modeset(struct wlr_drm_output *out, struct wlr_drm_mode *mode);

void wlr_drm_output_begin(struct wlr_drm_output *out);
void wlr_drm_output_end(struct wlr_drm_output *out);

#endif
