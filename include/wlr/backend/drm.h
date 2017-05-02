#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>

struct wlr_drm_backend;
struct wlr_drm_display;

struct wlr_drm_backend *wlr_drm_backend_init(struct wl_listener *add,
		struct wl_listener *rem,
		struct wl_listener *render);
void wlr_drm_backend_free(struct wlr_drm_backend *backend);

struct wl_event_loop *wlr_drm_backend_get_event_loop(struct wlr_drm_backend *backend);

bool wlr_drm_display_modeset(struct wlr_drm_display *disp, const char *str);
void wlr_drm_display_begin(struct wlr_drm_display *disp);
void wlr_drm_display_end(struct wlr_drm_display *disp);

#endif
