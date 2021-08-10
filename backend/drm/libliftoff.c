#include <libliftoff.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"

static bool create_mode_blob(struct wlr_drm_backend *drm,
		const struct wlr_drm_connector_state *state, uint32_t *blob_id) {
	if (!state->active) {
		*blob_id = 0;
		return true;
	}

	if (drmModeCreatePropertyBlob(drm->fd, &state->mode,
			sizeof(state->mode), blob_id)) {
		wlr_log_errno(WLR_ERROR, "Unable to create mode property blob");
		return false;
	}

	return true;
}

static bool add_prop(drmModeAtomicReq *req, uint32_t obj,
		uint32_t prop, uint64_t val) {
	if (drmModeAtomicAddProperty(req, obj, prop, val) < 0) {
		wlr_log_errno(WLR_ERROR, "drmModeAtomicAddProperty failed");
		return false;
	}
	return true;
}

static void commit_blob(struct wlr_drm_backend *drm,
		uint32_t *current, uint32_t next) {
	if (*current == next) {
		return;
	}
	if (*current != 0) {
		drmModeDestroyPropertyBlob(drm->fd, *current);
	}
	*current = next;
}

static void rollback_blob(struct wlr_drm_backend *drm,
		uint32_t *current, uint32_t next) {
	if (*current == next) {
		return;
	}
	if (next != 0) {
		drmModeDestroyPropertyBlob(drm->fd, next);
	}
}

static bool set_plane_props(struct wlr_drm_plane *plane,
		struct liftoff_layer *layer, int32_t x, int32_t y, uint64_t zpos) {
	struct wlr_drm_fb *fb = plane_get_next_fb(plane);
	if (fb == NULL) {
		wlr_log(WLR_ERROR, "Failed to acquire FB");
		return false;
	}

	uint32_t width = gbm_bo_get_width(fb->bo);
	uint32_t height = gbm_bo_get_height(fb->bo);

	return liftoff_layer_set_property(layer, "zpos", zpos) == 0 &&
		liftoff_layer_set_property(layer, "SRC_X", 0) == 0 &&
		liftoff_layer_set_property(layer, "SRC_Y", 0) == 0 &&
		liftoff_layer_set_property(layer, "SRC_W", (uint64_t)width << 16) == 0 &&
		liftoff_layer_set_property(layer, "SRC_H", (uint64_t)height << 16) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_X", (uint64_t)x) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_Y", (uint64_t)y) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_W", width) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_H", height) == 0 &&
		liftoff_layer_set_property(layer, "FB_ID", fb->id) == 0;
}

static bool disable_plane(struct wlr_drm_plane *plane) {
	return liftoff_layer_set_property(plane->liftoff_layer, "FB_ID", 0) == 0;
}

static bool liftoff_crtc_commit(struct wlr_drm_connector *conn,
		const struct wlr_drm_connector_state *state, uint32_t flags,
		bool test_only) {
	struct wlr_drm_backend *drm = conn->backend;
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (test_only) {
		flags |= DRM_MODE_ATOMIC_TEST_ONLY;
	}
	if (state->modeset) {
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	} else if (!test_only) {
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	}

	uint32_t mode_id = crtc->mode_id;
	if (state->modeset) {
		if (!create_mode_blob(drm, state, &mode_id)) {
			return false;
		}
	}

	drmModeAtomicReq *req = drmModeAtomicAlloc();
	if (req == NULL) {
		wlr_log(WLR_ERROR, "drmModeAtomicAlloc failed");
		return false;
	}

	uint32_t crtc_id = state->active ? crtc->id : 0;
	bool ok = add_prop(req, conn->id, conn->props.crtc_id, crtc_id) &&
		add_prop(req, crtc->id, crtc->props.mode_id, mode_id) &&
		add_prop(req, crtc->id, crtc->props.active, state->active);

	if (state->active) {
		ok = ok &&
			set_plane_props(crtc->primary, crtc->primary->liftoff_layer, 0, 0, 0) &&
			set_plane_props(crtc->primary, crtc->liftoff_composition_layer, 0, 0, 0);
		if (crtc->cursor) {
			if (drm_connector_is_cursor_visible(conn)) {
				ok = ok && set_plane_props(crtc->cursor,
					crtc->cursor->liftoff_layer,
					conn->cursor_x, conn->cursor_y, 1);
			} else {
				ok = ok && disable_plane(crtc->cursor);
			}
		}
	} else {
		ok = ok && disable_plane(crtc->primary);
		if (crtc->cursor) {
			ok = ok && disable_plane(crtc->cursor);
		}
	}

	if (!ok) {
		goto out;
	}

	int ret = liftoff_output_apply(crtc->liftoff, req, flags);
	if (ret != 0) {
		wlr_drm_conn_log(conn, test_only ? WLR_DEBUG : WLR_ERROR,
			"liftoff_output_apply failed: %s", strerror(-ret));
		ok = false;
		goto out;
	}

	if (crtc->cursor &&
			liftoff_layer_needs_composition(crtc->cursor->liftoff_layer)) {
		wlr_drm_conn_log(conn, WLR_DEBUG, "Failed to scan-out cursor plane");
		ok = false;
		goto out;
	}

	ret = drmModeAtomicCommit(drm->fd, req, flags, drm);
	if (ret != 0) {
		wlr_drm_conn_log_errno(conn, test_only ? WLR_DEBUG : WLR_ERROR,
			"Atomic %s failed (%s)",
			test_only ? "test" : "commit",
			state->modeset ? "modeset" : "pageflip");
		ok = false;
	}

out:

	drmModeAtomicFree(req);

	if (ok && !test_only) {
		commit_blob(drm, &crtc->mode_id, mode_id);
	} else {
		rollback_blob(drm, &crtc->mode_id, mode_id);
	}

	return ok;
}

const struct wlr_drm_interface libliftoff_iface = {
	.crtc_commit = liftoff_crtc_commit,
};
