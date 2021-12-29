/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_output.h>

/**
 * Creates a DRM backend using the specified GPU file descriptor (typically from
 * a device node in /dev/dri).
 *
 * To slave this to another DRM backend, pass it as the parent (which _must_ be
 * a DRM backend, other kinds of backends raise SIGABRT).
 */
struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
	struct wlr_session *session, struct wlr_device *dev,
	struct wlr_backend *parent);

bool wlr_backend_is_drm(struct wlr_backend *backend);
bool wlr_output_is_drm(struct wlr_output *output);

/**
 * Get the KMS connector object ID.
 */
uint32_t wlr_drm_connector_get_id(struct wlr_output *output);

/**
 * Tries to open non-master DRM FD. The compositor must not call `drmSetMaster`
 * on the returned FD.
 * Returns a valid opened DRM FD, or -1 on error.
 */
int wlr_drm_backend_get_non_master_fd(struct wlr_backend *backend);

/**
 * Leases a given output to the caller. The output must be from the associated
 * DRM backend.
 * Returns a valid opened DRM FD or -1 on error.
 */
int wlr_drm_create_lease(struct wlr_output **outputs, size_t n_outputs,
		uint32_t *lessee_id);

/**
 * Terminates a given lease. The output will be owned again by the backend
 */
bool wlr_drm_backend_terminate_lease(struct wlr_backend *backend,
	uint32_t lessee_id);

/**
 * Add mode to the list of available modes
 */
typedef struct _drmModeModeInfo drmModeModeInfo;
struct wlr_output_mode *wlr_drm_connector_add_mode(struct wlr_output *output,
	const drmModeModeInfo *mode);

#endif
