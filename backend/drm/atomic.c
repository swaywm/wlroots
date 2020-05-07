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

static bool atomic_end(int drm_fd, uint32_t flags, struct atomic *atom) {
	if (atom->failed) {
		return false;
	}

	flags |= DRM_MODE_ATOMIC_TEST_ONLY;
	if (drmModeAtomicCommit(drm_fd, atom->req, flags, NULL)) {
		wlr_log_errno(WLR_DEBUG, "Atomic test failed");
		drmModeAtomicSetCursor(atom->req, atom->cursor);
		return false;
	}

	return true;
}

static bool atomic_commit(int drm_fd, struct atomic *atom,
		struct wlr_drm_connector *conn, uint32_t flags, bool modeset) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);
	if (atom->failed) {
		return false;
	}

	int ret = drmModeAtomicCommit(drm_fd, atom->req, flags, drm);
	if (ret) {
		wlr_log_errno(WLR_ERROR, "%s: Atomic commit failed (%s)",
			conn->output.name, modeset ? "modeset" : "pageflip");
	}

	drmModeAtomicSetCursor(atom->req, 0);

	return !ret;
}

static void atomic_add(struct atomic *atom, uint32_t id, uint32_t prop, uint64_t val) {
	if (!atom->failed && drmModeAtomicAddProperty(atom->req, id, prop, val) < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to add atomic DRM property");
		atom->failed = true;
	}
}

static bool create_mode_blob(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, uint32_t *blob_id) {
	if (!crtc->active) {
		*blob_id = 0;
		return true;
	}

	if (drmModeCreatePropertyBlob(drm->fd, &crtc->mode,
			sizeof(drmModeModeInfo), blob_id)) {
		wlr_log_errno(WLR_ERROR, "Unable to create mode property blob");
		return false;
	}

	return true;
}

static bool create_gamma_lut_blob(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, uint32_t *blob_id) {
	if (crtc->gamma_table_size == 0) {
		*blob_id = 0;
		return true;
	}

	uint32_t size = crtc->gamma_table_size;
	uint16_t *r = crtc->gamma_table;
	uint16_t *g = crtc->gamma_table + size;
	uint16_t *b = crtc->gamma_table + 2 * size;

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

	if (drmModeCreatePropertyBlob(drm->fd, gamma,
			size * sizeof(struct drm_color_lut), blob_id) != 0) {
		wlr_log_errno(WLR_ERROR, "Unable to create property blob");
		free(gamma);
		return false;
	}
	free(gamma);

	return true;
}

static void plane_disable(struct atomic *atom, struct wlr_drm_plane *plane) {
	uint32_t id = plane->id;
	const union wlr_drm_plane_props *props = &plane->props;
	atomic_add(atom, id, props->fb_id, 0);
	atomic_add(atom, id, props->crtc_id, 0);
}

static void set_plane_props(struct atomic *atom, struct wlr_drm_backend *drm,
		struct wlr_drm_plane *plane, uint32_t crtc_id, int32_t x, int32_t y) {
	uint32_t id = plane->id;
	const union wlr_drm_plane_props *props = &plane->props;
	struct wlr_drm_fb *fb = plane_get_next_fb(plane);
	struct gbm_bo *bo = drm_fb_acquire(fb, drm, &plane->mgpu_surf);
	if (!bo) {
		goto error;
	}

	uint32_t fb_id = get_fb_for_bo(bo, drm->addfb2_modifiers);
	if (!fb_id) {
		goto error;
	}

	// The src_* properties are in 16.16 fixed point
	atomic_add(atom, id, props->src_x, 0);
	atomic_add(atom, id, props->src_y, 0);
	atomic_add(atom, id, props->src_w, (uint64_t)plane->surf.width << 16);
	atomic_add(atom, id, props->src_h, (uint64_t)plane->surf.height << 16);
	atomic_add(atom, id, props->crtc_w, plane->surf.width);
	atomic_add(atom, id, props->crtc_h, plane->surf.height);
	atomic_add(atom, id, props->fb_id, fb_id);
	atomic_add(atom, id, props->crtc_id, crtc_id);
	atomic_add(atom, id, props->crtc_x, (uint64_t)x);
	atomic_add(atom, id, props->crtc_y, (uint64_t)y);

	return;

error:
	atom->failed = true;
}

static bool atomic_crtc_commit(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (crtc->pending & WLR_DRM_CRTC_MODE) {
		if (crtc->mode_id != 0) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->mode_id);
		}

		if (!create_mode_blob(drm, crtc, &crtc->mode_id)) {
			return false;
		}
	}

	if (crtc->pending & WLR_DRM_CRTC_GAMMA_LUT) {
		// Fallback to legacy gamma interface when gamma properties are not available
		// (can happen on older Intel GPUs that support gamma but not degamma).
		if (crtc->props.gamma_lut == 0) {
			if (!drm_legacy_crtc_set_gamma(drm, crtc)) {
				return false;
			}
		} else {
			if (crtc->gamma_lut != 0) {
				drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_lut);
			}

			if (!create_gamma_lut_blob(drm, crtc, &crtc->gamma_lut)) {
				return false;
			}
		}
	}

	bool modeset = crtc->pending & WLR_DRM_CRTC_MODE;
	if (modeset) {
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	} else {
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	}

	struct atomic atom;
	atomic_begin(crtc, &atom);
	atomic_add(&atom, conn->id, conn->props.crtc_id,
		crtc->active ? crtc->id : 0);
	if (modeset && crtc->active && conn->props.link_status != 0) {
		atomic_add(&atom, conn->id, conn->props.link_status,
			DRM_MODE_LINK_STATUS_GOOD);
	}
	atomic_add(&atom, crtc->id, crtc->props.mode_id, crtc->mode_id);
	atomic_add(&atom, crtc->id, crtc->props.active, crtc->active);
	if (crtc->active) {
		atomic_add(&atom, crtc->id, crtc->props.gamma_lut, crtc->gamma_lut);
		set_plane_props(&atom, drm, crtc->primary, crtc->id, 0, 0);
		if (crtc->cursor) {
			if (crtc->cursor->cursor_enabled) {
				set_plane_props(&atom, drm, crtc->cursor, crtc->id,
					conn->cursor_x, conn->cursor_y);
			} else {
				plane_disable(&atom, crtc->cursor);
			}
		}
	} else {
		plane_disable(&atom, crtc->primary);
		if (crtc->cursor) {
			plane_disable(&atom, crtc->cursor);
		}
	}

	if (!atomic_end(drm->fd, modeset ? DRM_MODE_ATOMIC_ALLOW_MODESET : 0,
			&atom)) {
		drmModeAtomicSetCursor(atom.req, 0);
		return false;
	}

	if (!atomic_commit(drm->fd, &atom, conn, flags, modeset)) {
		return false;
	}

	if (crtc->active && crtc->cursor) {
		drm_fb_move(&crtc->cursor->queued_fb, &crtc->cursor->pending_fb);
	}
	return true;
}

static bool atomic_crtc_set_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo) {
	/* Cursor updates happen when we pageflip */
	return true;
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
	.crtc_commit = atomic_crtc_commit,
	.crtc_set_cursor = atomic_crtc_set_cursor,
	.crtc_get_gamma_size = atomic_crtc_get_gamma_size,
};
