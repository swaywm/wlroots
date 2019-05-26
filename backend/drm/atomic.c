#include <gbm.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

struct atomic {
	drmModeAtomicReq *req;
	int cursor;
	bool failed;
};

static void atomic_begin(struct wlr_drm_crtc *crtc, struct atomic *atom) {
	if (!crtc->atomic) {
		crtc->atomic = drmModeAtomicAlloc();
		if (!crtc->atomic) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			atom->failed = true;
			return;
		}
	}

	atom->req = crtc->atomic;
	atom->cursor = drmModeAtomicGetCursor(atom->req);
	atom->failed = false;
}

static bool atomic_end(int drm_fd, struct atomic *atom) {
	if (atom->failed) {
		return false;
	}

	uint32_t flags = DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_NONBLOCK;
	if (drmModeAtomicCommit(drm_fd, atom->req, flags, NULL)) {
		wlr_log_errno(WLR_ERROR, "Atomic test failed");
		drmModeAtomicSetCursor(atom->req, atom->cursor);
		return false;
	}

	return true;
}

static bool atomic_commit(int drm_fd, struct atomic *atom,
		struct wlr_drm_connector *conn, uint32_t flags, bool modeset) {
	if (atom->failed) {
		return false;
	}

	int ret = drmModeAtomicCommit(drm_fd, atom->req, flags, conn);
	if (ret) {
		wlr_log_errno(WLR_ERROR, "%s: Atomic commit failed (%s)",
			conn->output.name, modeset ? "modeset" : "pageflip");

		// Try to commit without new changes
		drmModeAtomicSetCursor(atom->req, atom->cursor);
		if (drmModeAtomicCommit(drm_fd, atom->req, flags, conn)) {
			wlr_log_errno(WLR_ERROR,
				"%s: Atomic commit without new changes failed (%s)",
				conn->output.name, modeset ? "modeset" : "pageflip");
		}
	}

	drmModeAtomicSetCursor(atom->req, 0);

	return !ret;
}

static inline void atomic_add(struct atomic *atom, uint32_t id, uint32_t prop, uint64_t val) {
	if (!atom->failed && drmModeAtomicAddProperty(atom->req, id, prop, val) < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to add atomic DRM property");
		atom->failed = true;
	}
}

static void set_plane_props(struct atomic *atom, struct wlr_drm_plane *plane,
		uint32_t crtc_id, uint32_t fb_id, bool set_crtc_xy) {
	uint32_t id = plane->id;
	const union wlr_drm_plane_props *props = &plane->props;

	// The src_* properties are in 16.16 fixed point
	atomic_add(atom, id, props->src_x, 0);
	atomic_add(atom, id, props->src_y, 0);
	atomic_add(atom, id, props->src_w, (uint64_t)plane->surf.width << 16);
	atomic_add(atom, id, props->src_h, (uint64_t)plane->surf.height << 16);
	atomic_add(atom, id, props->crtc_w, plane->surf.width);
	atomic_add(atom, id, props->crtc_h, plane->surf.height);
	atomic_add(atom, id, props->fb_id, fb_id);
	atomic_add(atom, id, props->crtc_id, crtc_id);
	if (set_crtc_xy) {
		atomic_add(atom, id, props->crtc_x, 0);
		atomic_add(atom, id, props->crtc_y, 0);
	}
}

static bool atomic_crtc_pageflip(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn,
		struct wlr_drm_crtc *crtc,
		uint32_t fb_id, drmModeModeInfo *mode) {
	if (mode != NULL) {
		if (crtc->mode_id != 0) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->mode_id);
		}

		if (drmModeCreatePropertyBlob(drm->fd, mode, sizeof(*mode),
				&crtc->mode_id)) {
			wlr_log_errno(WLR_ERROR, "Unable to create property blob");
			return false;
		}
	}

	uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;
	if (mode != NULL) {
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	} else {
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	}

	struct atomic atom;
	atomic_begin(crtc, &atom);
	atomic_add(&atom, conn->id, conn->props.crtc_id, crtc->id);
	if (mode != NULL && conn->props.link_status != 0) {
		atomic_add(&atom, conn->id, conn->props.link_status,
			DRM_MODE_LINK_STATUS_GOOD);
	}
	atomic_add(&atom, crtc->id, crtc->props.mode_id, crtc->mode_id);
	atomic_add(&atom, crtc->id, crtc->props.active, 1);
	set_plane_props(&atom, crtc->primary, crtc->id, fb_id, true);
	return atomic_commit(drm->fd, &atom, conn, flags, mode);
}

static bool atomic_conn_enable(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, bool enable) {
	struct wlr_drm_crtc *crtc = conn->crtc;
	if (crtc == NULL) {
		return !enable;
	}

	struct atomic atom;
	atomic_begin(crtc, &atom);
	atomic_add(&atom, crtc->id, crtc->props.active, enable);
	if (enable) {
		atomic_add(&atom, conn->id, conn->props.crtc_id, crtc->id);
		atomic_add(&atom, crtc->id, crtc->props.mode_id, crtc->mode_id);
	} else {
		atomic_add(&atom, conn->id, conn->props.crtc_id, 0);
		atomic_add(&atom, crtc->id, crtc->props.mode_id, 0);
	}
	return atomic_commit(drm->fd, &atom, conn, DRM_MODE_ATOMIC_ALLOW_MODESET,
		true);
}

bool legacy_crtc_set_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo);

static bool atomic_crtc_set_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo) {
	if (!crtc || !crtc->cursor) {
		return true;
	}

	struct wlr_drm_plane *plane = crtc->cursor;
	// We can't use atomic operations on fake planes
	if (plane->id == 0) {
		return legacy_crtc_set_cursor(drm, crtc, bo);
	}

	struct atomic atom;

	atomic_begin(crtc, &atom);

	if (bo) {
		uint32_t fb_id =
			get_fb_for_bo(bo, plane->drm_format, drm->addfb2_modifiers);
		set_plane_props(&atom, plane, crtc->id, fb_id, false);
	} else {
		atomic_add(&atom, plane->id, plane->props.fb_id, 0);
		atomic_add(&atom, plane->id, plane->props.crtc_id, 0);
	}

	return atomic_end(drm->fd, &atom);
}

bool legacy_crtc_move_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, int x, int y);

static bool atomic_crtc_move_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, int x, int y) {
	if (!crtc || !crtc->cursor) {
		return true;
	}

	struct wlr_drm_plane *plane = crtc->cursor;
	// We can't use atomic operations on fake planes
	if (plane->id == 0) {
		return legacy_crtc_move_cursor(drm, crtc, x, y);
	}

	struct atomic atom;

	atomic_begin(crtc, &atom);
	atomic_add(&atom, plane->id, plane->props.crtc_x, x);
	atomic_add(&atom, plane->id, plane->props.crtc_y, y);
	return atomic_end(drm->fd, &atom);
}

static bool atomic_crtc_set_gamma(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, size_t size,
		uint16_t *r, uint16_t *g, uint16_t *b) {
	// Fallback to legacy gamma interface when gamma properties are not available
	// (can happen on older Intel GPUs that support gamma but not degamma).
	// TEMP: This is broken on AMDGPU. Provide a fallback to legacy until they
	// get it fixed. Ref https://bugs.freedesktop.org/show_bug.cgi?id=107459
	const char *no_atomic_str = getenv("WLR_DRM_NO_ATOMIC_GAMMA");
	bool no_atomic = no_atomic_str != NULL && strcmp(no_atomic_str, "1") == 0;
	if (crtc->props.gamma_lut == 0 || no_atomic) {
		return legacy_iface.crtc_set_gamma(drm, crtc, size, r, g, b);
	}

	struct drm_color_lut *gamma = malloc(size * sizeof(struct drm_color_lut));
	if (gamma == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate gamma table");
		return false;
	}

	for (size_t i = 0; i < size; i++) {
		gamma[i].red = r[i];
		gamma[i].green = g[i];
		gamma[i].blue = b[i];
	}

	if (crtc->gamma_lut != 0) {
		drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_lut);
	}

	if (drmModeCreatePropertyBlob(drm->fd, gamma,
			size * sizeof(struct drm_color_lut), &crtc->gamma_lut)) {
		free(gamma);
		wlr_log_errno(WLR_ERROR, "Unable to create property blob");
		return false;
	}
	free(gamma);

	struct atomic atom;
	atomic_begin(crtc, &atom);
	atomic_add(&atom, crtc->id, crtc->props.gamma_lut, crtc->gamma_lut);
	return atomic_end(drm->fd, &atom);
}

static size_t atomic_crtc_get_gamma_size(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	if (crtc->props.gamma_lut_size == 0) {
		return legacy_iface.crtc_get_gamma_size(drm, crtc);
	}

	uint64_t gamma_lut_size;
	if (!get_drm_prop(drm->fd, crtc->id, crtc->props.gamma_lut_size,
			&gamma_lut_size)) {
		wlr_log(WLR_ERROR, "Unable to get gamma lut size");
		return 0;
	}

	return (size_t)gamma_lut_size;
}

const struct wlr_drm_interface atomic_iface = {
	.conn_enable = atomic_conn_enable,
	.crtc_pageflip = atomic_crtc_pageflip,
	.crtc_set_cursor = atomic_crtc_set_cursor,
	.crtc_move_cursor = atomic_crtc_move_cursor,
	.crtc_set_gamma = atomic_crtc_set_gamma,
	.crtc_get_gamma_size = atomic_crtc_get_gamma_size,
};
