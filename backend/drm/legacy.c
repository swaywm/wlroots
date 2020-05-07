#include <gbm.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

static bool legacy_crtc_commit(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_drm_crtc *crtc = conn->crtc;
	struct wlr_drm_plane *cursor = crtc->cursor;

	uint32_t fb_id = 0;
	if (crtc->active) {
		struct wlr_drm_fb *fb = plane_get_next_fb(crtc->primary);
		struct gbm_bo *bo = drm_fb_acquire(fb, drm, &crtc->primary->mgpu_surf);
		if (!bo) {
			return false;
		}

		fb_id = get_fb_for_bo(bo, drm->addfb2_modifiers);
		if (!fb_id) {
			return false;
		}
	}

	if (crtc->pending & WLR_DRM_CRTC_MODE) {
		uint32_t *conns = NULL;
		size_t conns_len = 0;
		drmModeModeInfo *mode = NULL;
		if (crtc->active) {
			conns = &conn->id;
			conns_len = 1;
			mode = &crtc->mode;
		}

		if (drmModeConnectorSetProperty(drm->fd, conn->id, conn->props.dpms,
				crtc->active ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF) != 0) {
			wlr_log_errno(WLR_ERROR, "%s: failed to set DPMS property",
				conn->output.name);
			return false;
		}

		if (drmModeSetCrtc(drm->fd, crtc->id, fb_id, 0, 0,
				conns, conns_len, mode)) {
			wlr_log_errno(WLR_ERROR, "%s: failed to set CRTC",
				conn->output.name);
			return false;
		}
	}

	if (crtc->pending & WLR_DRM_CRTC_GAMMA_LUT) {
		if (!drm_legacy_crtc_set_gamma(drm, crtc)) {
			return false;
		}
	}

	if (cursor != NULL && cursor->cursor_enabled && drmModeMoveCursor(drm->fd,
			crtc->id, conn->cursor_x, conn->cursor_y) != 0) {
		wlr_log_errno(WLR_ERROR, "%s: failed to move cursor", conn->output.name);
		return false;
	}

	if (flags & DRM_MODE_PAGE_FLIP_EVENT) {
		if (drmModePageFlip(drm->fd, crtc->id, fb_id,
				DRM_MODE_PAGE_FLIP_EVENT, drm)) {
			wlr_log_errno(WLR_ERROR, "%s: Failed to page flip", conn->output.name);
			return false;
		}
	}

	return true;
}

bool legacy_crtc_set_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo) {
	if (!crtc || !crtc->cursor) {
		return true;
	}

	if (!bo) {
		if (drmModeSetCursor(drm->fd, crtc->id, 0, 0, 0)) {
			wlr_log_errno(WLR_DEBUG, "Failed to clear hardware cursor");
			return false;
		}
		return true;
	}

	struct wlr_drm_plane *plane = crtc->cursor;

	if (drmModeSetCursor(drm->fd, crtc->id, gbm_bo_get_handle(bo).u32,
			plane->surf.width, plane->surf.height)) {
		wlr_log_errno(WLR_DEBUG, "Failed to set hardware cursor");
		return false;
	}

	drm_fb_move(&crtc->cursor->queued_fb, &crtc->cursor->pending_fb);
	return true;
}

bool drm_legacy_crtc_set_gamma(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	uint32_t size = crtc->gamma_table_size;
	uint16_t *r = NULL, *g = NULL, *b = NULL;
	if (size > 0) {
		r = crtc->gamma_table;
		g = crtc->gamma_table + crtc->gamma_table_size;
		b = crtc->gamma_table + 2 * crtc->gamma_table_size;
	}

	if (drmModeCrtcSetGamma(drm->fd, crtc->id, size, r, g, b) != 0) {
		wlr_log_errno(WLR_ERROR, "Failed to set gamma LUT on CRTC %"PRIu32,
			crtc->id);
		return false;
	}
	return true;
}

static size_t legacy_crtc_get_gamma_size(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	return (size_t)crtc->legacy_crtc->gamma_size;
}

const struct wlr_drm_interface legacy_iface = {
	.crtc_commit = legacy_crtc_commit,
	.crtc_set_cursor = legacy_crtc_set_cursor,
	.crtc_get_gamma_size = legacy_crtc_get_gamma_size,
};
