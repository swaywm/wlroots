#ifndef DRM_H
#define DRM_H

#include <stdbool.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <gbm.h>

#include "backend/egl.h"
#include "backend.h"

struct wlr_drm_renderer {
	int fd;
	struct gbm_device *gbm;
	struct wlr_egl egl;
};

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd);
void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer);

enum wlr_drm_output_state {
	DRM_OUTPUT_DISCONNECTED,
	DRM_OUTPUT_NEEDS_MODESET,
	DRM_OUTPUT_CONNECTED,
};

struct wlr_output_mode_state {
	struct wlr_wl_output_mode *wlr_mode;
	drmModeModeInfo mode;
};

struct wlr_output_state {
	struct wlr_output *wlr_output;
	enum wlr_drm_output_state state;
	uint32_t connector;
	char name[16];

	struct {
		uint32_t dpms;
	} props;

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

void wlr_drm_output_cleanup(struct wlr_output_state *output, bool restore);
void wlr_drm_output_dpms(int fd, struct wlr_output_state *output, bool screen_on);

void wlr_drm_scan_connectors(struct wlr_backend_state *state);
int wlr_drm_event(int fd, uint32_t mask, void *data);

void wlr_drm_output_draw_blank(struct wlr_output_state *output);

#endif
