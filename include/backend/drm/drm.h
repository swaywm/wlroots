#ifndef DRM_H
#define DRM_H

#include <stdbool.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <gbm.h>

#include "backend/egl.h"

struct wlr_drm_renderer {
	int fd;

	// Currently here so that rendering has access to the event queue.
	// Ideally this is will be removed later once the way events are
	// handled is changed.
	struct wlr_drm_backend *backend;

	struct gbm_device *gbm;
	struct wlr_egl egl;
};

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd);
void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer);

enum wlr_drm_output_state {
	DRM_OUTPUT_INVALID,
	DRM_OUTPUT_DISCONNECTED,
	DRM_OUTPUT_NEEDS_MODESET,
	DRM_OUTPUT_CONNECTED,
};

struct wlr_drm_output {
	enum wlr_drm_output_state state;
	uint32_t connector;
	char name[16];

	size_t num_modes;
	drmModeModeInfo *modes;
	drmModeModeInfo *active_mode;

	uint32_t width;
	uint32_t height;

	uint32_t crtc;
	drmModeCrtc *old_crtc;

	struct wlr_drm_renderer *renderer;
	struct gbm_surface *gbm;
	EGLSurface *egl;

	bool pageflip_pending;
	bool cleanup;
};

bool wlr_drm_output_modeset(struct wlr_drm_output *out, const char *str);
void wlr_drm_output_free(struct wlr_drm_output *out, bool restore);

void wlr_drm_output_begin(struct wlr_drm_output *out);
void wlr_drm_output_end(struct wlr_drm_output *out);

void wlr_drm_scan_connectors(struct wlr_drm_backend *backend);
int wlr_drm_event(int fd, uint32_t mask, void *data);

#endif
