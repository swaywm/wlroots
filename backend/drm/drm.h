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

#include <wlr/backend/session.h>
#include <wlr/backend/drm.h>
#include <wlr/util/list.h>

#include <backend/egl.h>
#include <backend/udev.h>
#include "drm-properties.h"

struct wlr_drm_plane {
	uint32_t type;
	uint32_t id;
	uint32_t fb_id;

	uint32_t possible_crtcs;

	int32_t x, y;
	uint32_t width, height;

	struct gbm_surface *gbm;
	EGLSurface egl;

	struct gbm_bo *front;
	struct gbm_bo *back;

	union wlr_drm_plane_props props;
};

struct wlr_drm_crtc {
	uint32_t id;
	union {
		struct {
			struct wlr_drm_plane *overlay;
			struct wlr_drm_plane *primary;
			struct wlr_drm_plane *cursor;
		};
		struct wlr_drm_plane *planes[3];
	};

	union wlr_drm_crtc_props props;

	struct wl_list connectors;
};

struct wlr_drm_connector {
	struct wlr_output *base;
	uint32_t id;
	struct wlr_drm_crtc *crtc;

	union wlr_drm_connector_props props;

	struct wl_list link;
};

struct wlr_drm_renderer {
	int fd;
	struct gbm_device *gbm;
	struct wlr_egl egl;
};

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd);
void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer);

struct wlr_backend_state {
	struct wlr_backend *base;

	int fd;
	dev_t dev;

	size_t num_crtcs;
	struct wlr_drm_crtc *crtcs;
	size_t num_planes;
	struct wlr_drm_plane *planes;

	union {
		struct {
			size_t num_overlay_planes;
			size_t num_primary_planes;
			size_t num_cursor_planes;
		};
		size_t num_type_planes[3];
	};

	union {
		struct {
			struct wlr_drm_plane *overlay_planes;
			struct wlr_drm_plane *primary_planes;
			struct wlr_drm_plane *cursor_planes;
		};
		struct wlr_drm_plane *type_planes[3];
	};

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
	WLR_DRM_OUTPUT_DISCONNECTED,
	WLR_DRM_OUTPUT_NEEDS_MODESET,
	WLR_DRM_OUTPUT_CONNECTED,
};

struct wlr_output_mode_state {
	struct wlr_wl_output_mode *wlr_mode;
	drmModeModeInfo mode;
};

struct wlr_output_state {
	struct wlr_output *base;
	enum wlr_drm_output_state state;
	uint32_t connector;

	struct wlr_drm_crtc *crtc;
	uint32_t possible_crtc;

	union wlr_drm_connector_props props;

	uint32_t width;
	uint32_t height;

	drmModeCrtc *old_crtc;

	struct wlr_drm_renderer *renderer;
	struct gbm_bo *bo[2];
	struct gbm_bo *cursor_bo[2];
	int current_cursor;
	uint32_t cursor_width, cursor_height;

	bool pageflip_pending;
	bool cleanup;
};

bool wlr_drm_init_resources(struct wlr_backend_state *drm);
void wlr_drm_output_cleanup(struct wlr_output_state *output, bool restore);

void wlr_drm_scan_connectors(struct wlr_backend_state *state);
int wlr_drm_event(int fd, uint32_t mask, void *data);

void wlr_drm_output_start_renderer(struct wlr_output_state *output);
void wlr_drm_output_pause_renderer(struct wlr_output_state *output);

#endif
