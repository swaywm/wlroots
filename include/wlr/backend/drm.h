#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>
#include <wlr/session.h>

struct wlr_drm_backend;
struct wlr_drm_output;

struct wlr_drm_backend *wlr_drm_backend_init(struct wlr_session *session,
	struct wl_listener *add, struct wl_listener *rem, struct wl_listener *render);
void wlr_drm_backend_free(struct wlr_drm_backend *backend);

struct wl_event_loop *wlr_drm_backend_get_event_loop(struct wlr_drm_backend *backend);

bool wlr_drm_output_modeset(struct wlr_drm_output *out, const char *str);
void wlr_drm_output_begin(struct wlr_drm_output *out);
void wlr_drm_output_end(struct wlr_drm_output *out);

#endif
