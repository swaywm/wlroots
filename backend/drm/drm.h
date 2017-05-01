#ifndef DRM_H
#define DRM_H

#include <stdbool.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <gbm.h>

enum otd_display_state {
	OTD_DISP_INVALID,
	OTD_DISP_DISCONNECTED,
	OTD_DISP_NEEDS_MODESET,
	OTD_DISP_CONNECTED,
};

struct otd_display {
	struct otd *otd;

	enum otd_display_state state;
	uint32_t connector;
	char name[16];

	size_t num_modes;
	drmModeModeInfo *modes;
	drmModeModeInfo *active_mode;

	uint32_t width;
	uint32_t height;

	uint32_t crtc;
	drmModeCrtc *old_crtc;

	struct gbm_surface *gbm;
	EGLSurface *egl;
	uint32_t fb_id;

	bool pageflip_pending;
	bool cleanup;
};

bool init_renderer(struct otd *otd);
void destroy_renderer(struct otd *otd);

void scan_connectors(struct otd *otd);
bool modeset_str(struct otd *otd, struct otd_display *disp, const char *str);
void destroy_display_renderer(struct otd *otd, struct otd_display *disp);

void get_drm_event(struct otd *otd);

void rendering_begin(struct otd_display *disp);
void rendering_end(struct otd_display *disp);

#endif
