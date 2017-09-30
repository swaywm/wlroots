#ifndef BACKEND_DRM_IFACE_H
#define BACKEND_DRM_IFACE_H

#include <stdbool.h>
#include <stdint.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct wlr_drm_backend;
struct wlr_drm_output;
struct wlr_drm_crtc;

// Used to provide atomic or legacy DRM functions
struct wlr_drm_interface {
	// Enable or disable DPMS for output
	void (*conn_enable)(struct wlr_drm_backend *backend,
			struct wlr_drm_output *output, bool enable);
	// Pageflip on crtc. If mode is non-NULL perform a full modeset using it.
	bool (*crtc_pageflip)(struct wlr_drm_backend *backend,
			struct wlr_drm_output *output, struct wlr_drm_crtc *crtc,
			uint32_t fb_id, drmModeModeInfo *mode);
	// Enable the cursor buffer on crtc. Set bo to NULL to disable
	bool (*crtc_set_cursor)(struct wlr_drm_backend *backend,
			struct wlr_drm_crtc *crtc, struct gbm_bo *bo);
	// Move the cursor on crtc
	bool (*crtc_move_cursor)(struct wlr_drm_backend *backend,
			struct wlr_drm_crtc *crtc, int x, int y);
};

extern const struct wlr_drm_interface iface_atomic;
extern const struct wlr_drm_interface iface_legacy;

#endif
