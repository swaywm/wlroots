#ifndef DRM_BACKEND_H
#define DRM_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <libudev.h>
#include <gbm.h>

#include <wlr/session.h>
#include <wlr/backend/drm.h>
#include <wlr/util/list.h>

#include "backend/egl.h"
#include "backend/udev.h"
#include "event.h"

struct wlr_drm_renderer {
	int fd;
	struct gbm_device *gbm;
	struct wlr_egl egl;
};

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd);
void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer);

struct wlr_backend_state {
	int fd;
	dev_t dev;

	struct wlr_backend *backend;
	struct wl_display *display;
	struct wl_event_source *drm_event;

	struct wl_listener session_signal;
	struct wl_listener drm_invalidated;

	uint32_t taken_crtcs;
	list_t *outputs;

	struct wlr_drm_renderer renderer;
	struct wlr_session *session;
	struct wlr_udev *udev;
};

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

	struct {
		uint32_t dpms;
	} props;

	uint32_t width;
	uint32_t height;

	uint32_t crtc;
	drmModeCrtc *old_crtc;

	struct wlr_drm_renderer *renderer;
	EGLSurface *egl;
	struct gbm_surface *gbm;
	struct gbm_bo *bo[2];
	struct gbm_bo *cursor_bo[2];
	int current_cursor;
	uint32_t cursor_width, cursor_height;

	bool pageflip_pending;
	bool cleanup;
};

void wlr_drm_output_cleanup(struct wlr_output_state *output, bool restore);

void wlr_drm_scan_connectors(struct wlr_backend_state *state);
int wlr_drm_event(int fd, uint32_t mask, void *data);

void wlr_drm_output_start_renderer(struct wlr_output_state *output);
void wlr_drm_output_pause_renderer(struct wlr_output_state *output);

#endif
