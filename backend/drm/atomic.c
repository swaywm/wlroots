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
	bool failed;
};

static void atomic_begin(struct atomic *atom) {
	memset(atom, 0, sizeof(*atom));

	atom->req = drmModeAtomicAlloc();
	if (!atom->req) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		atom->failed = true;
		return;
	}
}

static bool atomic_commit(struct atomic *atom,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);
	if (atom->failed) {
		return false;
	}

	int ret = drmModeAtomicCommit(drm->fd, atom->req, flags, drm);
	if (ret) {
		wlr_log_errno(WLR_ERROR, "%s: Atomic %s failed (%s)",
			conn->output.name,
			(flags & DRM_MODE_ATOMIC_TEST_ONLY) ? "test" : "commit",
			(flags & DRM_MODE_ATOMIC_ALLOW_MODESET) ? "modeset" : "pageflip");
		return false;
	}

	return true;
}

static void atomic_finish(struct atomic *atom) {
	drmModeAtomicFree(atom->req);
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
		size_t size, const uint16_t *lut, uint32_t *blob_id) {
	if (size == 0) {
		*blob_id = 0;
		return true;
	}

	struct drm_color_lut *gamma = malloc(size * sizeof(struct drm_color_lut));
	if (gamma == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate gamma table");
		return false;
	}

	const uint16_t *r = lut;
	const uint16_t *g = lut + size;
	const uint16_t *b = lut + 2 * size;
	for (size_t i = 0; i < size; i++) {
		gamma[i].red = r[i];
		gamma[i].green = g[i];
		gamma[i].blue = b[i];
	}

	if (drmModeCreatePropertyBlob(drm->fd, gamma,
			size * sizeof(struct drm_color_lut), blob_id) != 0) {
		wlr_log_errno(WLR_ERROR, "Unable to create gamma LUT property blob");
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
	wlr_log(WLR_ERROR, "Failed to set plane %"PRIu32" properties", plane->id);
	atom->failed = true;
}

static bool atomic_crtc_commit(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_output *output = &conn->output;
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (crtc->pending & WLR_DRM_CRTC_MODE) {
		if (crtc->mode_id != 0) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->mode_id);
		}

		if (!create_mode_blob(drm, crtc, &crtc->mode_id)) {
			return false;
		}
	}

	if (output->pending.committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		// Fallback to legacy gamma interface when gamma properties are not
		// available (can happen on older Intel GPUs that support gamma but not
		// degamma).
		if (crtc->props.gamma_lut == 0) {
			if (!drm_legacy_crtc_set_gamma(drm, crtc,
					output->pending.gamma_lut_size,
					output->pending.gamma_lut)) {
				return false;
			}
		} else {
			if (crtc->gamma_lut != 0) {
				drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_lut);
			}

			wlr_log(WLR_ERROR, "setting gamma LUT %zu", output->pending.gamma_lut_size);
			if (!create_gamma_lut_blob(drm, output->pending.gamma_lut_size,
					output->pending.gamma_lut, &crtc->gamma_lut)) {
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
	atomic_begin(&atom);
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

	bool ok = atomic_commit(&atom, conn, flags);
	atomic_finish(&atom);
	return ok;
}

const struct wlr_drm_interface atomic_iface = {
	.crtc_commit = atomic_crtc_commit,
};
