#ifndef BACKEND_DRM_DRM_H
#define BACKEND_DRM_DRM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <wlr/backend/drm.h>
#include <wlr/backend/session.h>
#include <wlr/render/egl.h>
#include <wlr/render/format_set.h>

#include "iface.h"
#include "properties.h"

struct wlr_drm_plane {
	uint32_t type;
	uint32_t id;
	uint32_t possible_crtcs;

	struct wlr_format_set formats;

	union wlr_drm_plane_props props;
};

struct wlr_drm_crtc {
	uint32_t id;
	uint32_t pipe;
	struct wlr_drm_plane primary;
	// cursor.id == 0 means cursor plane does not exist
	struct wlr_drm_plane cursor;

	union wlr_drm_crtc_props props;

	size_t max_gamma_size;
	union {
		uint32_t gamma_blob_id; // Have gamma property
		struct { // Don't have gamma property
			uint16_t *gamma_table;
			size_t gamma_size;
		};
	};

	// Only used during atomic_apply_state
	bool in_list;
	struct wl_list state_link;
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

	// Atomic modesetting only
	uint32_t blob_id;
};

struct wlr_drm_connector {
	struct wlr_output output;
	uint32_t id;
	uint32_t possible_crtcs;
	enum wlr_drm_connector_state state;

	bool desire_enabled;
	struct wlr_drm_mode *desired_mode;
	struct wlr_image *desired_img;
	struct wlr_image *locked_img;

	struct wlr_drm_crtc *crtc;

	union wlr_drm_connector_props props;

	uint32_t width, height;
	int32_t cursor_x, cursor_y;

	// So we can restore TTY property on exit
	drmModeCrtc *old_crtc;

	bool pageflip_pending;

	struct wl_event_source *retry_pageflip;
	struct wl_list link;

	// Only used during scan_drm_connectors
	bool seen;
	struct wl_list new_link;
	// Only used during atomic_apply_state
	struct wl_list state_link;
};

struct wlr_drm_backend {
	struct wlr_backend backend;

	struct wlr_drm_backend *parent;
	const struct wlr_drm_interface *iface;
	clockid_t clock;

	int fd;
	int render_fd;
	bool has_modifiers;

	/*
	 * Our modesetting test has a circular dependency, where in order to
	 * find out what CRTC we get, we need a buffer, and in order to get a
	 * buffer, we need to know what CRTC we have.
	 *
	 * In order to get around this, we instead use this 64x64 XRGB8888 dumb
	 * buffer for the first frame, and expect to be replaced properly after
	 * that.
	 *
	 * 64x64 was chosen somewhat arbirarily. Drivers seem to have issues trying
	 * to scan out a 1x1 buffer, so I just picked a value that I think would
	 * be well above whatever some portable minimum value is.
	 */
	struct wlr_image fake;

	size_t num_crtcs;
	struct wlr_drm_crtc *crtcs;
	size_t num_planes;
	struct wlr_drm_plane *planes;

	struct wl_display *display;
	struct wl_event_source *drm_event;

	struct wl_listener display_destroy;
	struct wl_listener session_signal;
	struct wl_listener drm_invalidated;

	struct wl_list outputs;

	struct wlr_session *session;

	bool delay_modeset;
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

#endif
