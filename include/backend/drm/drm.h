#ifndef BACKEND_DRM_DRM_H
#define BACKEND_DRM_DRM_H

#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/session.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/egl.h>
#include <xf86drmMode.h>
#include "iface.h"
#include "properties.h"
#include "renderer.h"

struct wlr_drm_plane {
	uint32_t type;
	uint32_t id;

	uint32_t possible_crtcs;

	struct wlr_drm_surface surf;
	struct wlr_drm_surface mgpu_surf;

	uint32_t drm_format; // ARGB8888 or XRGB8888
	struct wlr_drm_format_set formats;

	// Only used by cursor
	float matrix[9];
	bool cursor_enabled;
	int32_t cursor_hotspot_x, cursor_hotspot_y;

	union wlr_drm_plane_props props;
};

struct wlr_drm_crtc {
	uint32_t id;

	// Atomic modesetting only
	uint32_t mode_id;
	uint32_t gamma_lut;
	drmModeAtomicReq *atomic;

	// Legacy only
	drmModeCrtc *legacy_crtc;

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

	uint16_t *gamma_table;
	size_t gamma_table_size;
};

struct wlr_drm_backend {
	struct wlr_backend backend;

	struct wlr_drm_backend *parent;
	const struct wlr_drm_interface *iface;
	clockid_t clock;

	int fd;

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

	struct wl_listener display_destroy;
	struct wl_listener session_signal;
	struct wl_listener drm_invalidated;

	struct wl_list outputs;

	struct wlr_drm_renderer renderer;
	struct wlr_session *session;
};

enum wlr_drm_connector_state {
	// Connector is available but no output is plugged in
	WLR_DRM_CONN_DISCONNECTED,
	// An output just has been plugged in and is waiting for a modeset
	WLR_DRM_CONN_NEEDS_MODESET,
	WLR_DRM_CONN_CLEANUP,
	WLR_DRM_CONN_CONNECTED,
	// Connector disappeared, waiting for being destroyed on next page-flip
	WLR_DRM_CONN_DISAPPEARED,
};

struct wlr_drm_mode {
	struct wlr_output_mode wlr_mode;
	drmModeModeInfo drm_mode;
};

struct wlr_drm_connector {
	struct wlr_output output;

	enum wlr_drm_connector_state state;
	struct wlr_output_mode *desired_mode;
	bool desired_enabled;
	uint32_t id;

	struct wlr_drm_crtc *crtc;
	uint32_t possible_crtc;

	union wlr_drm_connector_props props;

	uint32_t width, height;
	int32_t cursor_x, cursor_y;

	drmModeCrtc *old_crtc;

	bool pageflip_pending;
	struct wl_event_source *retry_pageflip;
	struct wl_list link;
};

struct wlr_drm_backend *get_drm_backend_from_backend(
	struct wlr_backend *wlr_backend);
bool check_drm_features(struct wlr_drm_backend *drm);
bool init_drm_resources(struct wlr_drm_backend *drm);
void finish_drm_resources(struct wlr_drm_backend *drm);
void restore_drm_outputs(struct wlr_drm_backend *drm);
void scan_drm_connectors(struct wlr_drm_backend *state);
int handle_drm_event(int fd, uint32_t mask, void *data);
bool enable_drm_connector(struct wlr_output *output, bool enable);
bool set_drm_connector_gamma(struct wlr_output *output, size_t size,
	const uint16_t *r, const uint16_t *g, const uint16_t *b);
bool drm_connector_set_mode(struct wlr_output *output,
	struct wlr_output_mode *mode);

#endif
