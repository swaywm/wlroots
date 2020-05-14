#ifndef BACKEND_DRM_DRM_H
#define BACKEND_DRM_DRM_H

#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
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

	struct wlr_drm_surface surf;
	struct wlr_drm_surface mgpu_surf;

	/* Buffer to be submitted to the kernel on the next page-flip */
	struct wlr_drm_fb pending_fb;
	/* Buffer submitted to the kernel, will be presented on next vblank */
	struct wlr_drm_fb queued_fb;
	/* Buffer currently displayed on screen */
	struct wlr_drm_fb current_fb;

	uint32_t drm_format; // ARGB8888 or XRGB8888
	struct wlr_drm_format_set formats;

	// Only used by cursor
	bool cursor_enabled;
	int32_t cursor_hotspot_x, cursor_hotspot_y;

	union wlr_drm_plane_props props;
};

enum wlr_drm_crtc_field {
	WLR_DRM_CRTC_MODE = 1 << 0,
};

struct wlr_drm_crtc {
	uint32_t id;
	uint32_t pending; // bitfield of enum wlr_drm_crtc_field

	bool active;
	drmModeModeInfo mode;

	// Atomic modesetting only
	uint32_t mode_id;
	uint32_t gamma_lut;

	// Legacy only
	drmModeCrtc *legacy_crtc;

	struct wlr_drm_plane *primary;
	struct wlr_drm_plane *cursor;

	/*
	 * We don't support overlay planes yet, but we keep track of them to
	 * give to DRM lease clients.
	 */
	size_t num_overlays;
	uint32_t *overlays;

	union wlr_drm_crtc_props props;
};

struct wlr_drm_backend {
	struct wlr_backend backend;

	struct wlr_drm_backend *parent;
	const struct wlr_drm_interface *iface;
	clockid_t clock;
	bool addfb2_modifiers;

	int fd;

	size_t num_crtcs;
	struct wlr_drm_crtc *crtcs;

	struct wl_display *display;
	struct wl_event_source *drm_event;

	struct wl_listener display_destroy;
	struct wl_listener session_destroy;
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

	int32_t cursor_x, cursor_y;

	drmModeCrtc *old_crtc;

	struct wl_event_source *retry_pageflip;
	struct wl_list link;

	/*
	 * We've asked for a state change in the kernel, and yet to recieve a
	 * notification for its completion. Currently, the kernel only has a
	 * queue length of 1, and no way to modify your submissions after
	 * they're sent.
	 */
	bool pageflip_pending;
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
bool drm_connector_set_mode(struct wlr_output *output,
	struct wlr_output_mode *mode);
size_t drm_crtc_get_gamma_lut_size(struct wlr_drm_backend *drm,
	struct wlr_drm_crtc *crtc);

struct wlr_drm_fb *plane_get_next_fb(struct wlr_drm_plane *plane);

#endif
