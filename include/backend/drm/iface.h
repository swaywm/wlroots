#ifndef BACKEND_DRM_IFACE_H
#define BACKEND_DRM_IFACE_H

#include <gbm.h>
#include <stdbool.h>
#include <stdint.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct wlr_drm_backend;
struct wlr_drm_connector;
struct wlr_drm_crtc;

// Used to provide atomic or legacy DRM functions
struct wlr_drm_interface {
	// Enable or disable DPMS for connector
	bool (*conn_enable)(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, bool enable);
	// Pageflip on crtc.
	bool (*crtc_pageflip)(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn);
	// Enable the cursor buffer on crtc. Set bo to NULL to disable
	bool (*crtc_set_cursor)(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo);
	// Set the gamma lut on crtc
	bool (*crtc_set_gamma)(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, size_t size,
		uint16_t *r, uint16_t *g, uint16_t *b);
	// Get the gamma lut size of a crtc
	size_t (*crtc_get_gamma_size)(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc);
};

extern const struct wlr_drm_interface atomic_iface;
extern const struct wlr_drm_interface legacy_iface;

#endif
