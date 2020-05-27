#include <assert.h>
#include <gbm.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

static bool legacy_crtc_commit(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_output *output = &conn->output;
	struct wlr_drm_crtc *crtc = conn->crtc;
	struct wlr_drm_plane *cursor = crtc->cursor;

	uint32_t fb_id = 0;
	if (crtc->pending.active) {
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

	if (crtc->pending_modeset) {
		uint32_t *conns = NULL;
		size_t conns_len = 0;
		drmModeModeInfo *mode = NULL;
		if (crtc->pending.mode != NULL) {
			conns = &conn->id;
			conns_len = 1;
			mode = &crtc->pending.mode->drm_mode;
		}

		uint32_t dpms = crtc->pending.active ?
			DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF;
		if (drmModeConnectorSetProperty(drm->fd, conn->id, conn->props.dpms,
				dpms) != 0) {
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

	if (output->pending.committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		if (!drm_legacy_crtc_set_gamma(drm, crtc,
				output->pending.gamma_lut_size, output->pending.gamma_lut)) {
			return false;
		}
	}

	if (cursor != NULL && drm_connector_is_cursor_visible(conn)) {
		struct wlr_drm_fb *cursor_fb = plane_get_next_fb(cursor);
		struct gbm_bo *cursor_bo =
			drm_fb_acquire(cursor_fb, drm, &cursor->mgpu_surf);
		if (!cursor_bo) {
			wlr_log_errno(WLR_DEBUG, "%s: failed to acquire cursor FB",
				conn->output.name);
			return false;
		}

		if (drmModeSetCursor(drm->fd, crtc->id,
				gbm_bo_get_handle(cursor_bo).u32,
				cursor->surf.width, cursor->surf.height)) {
			wlr_log_errno(WLR_DEBUG, "%s: failed to set hardware cursor",
				conn->output.name);
			return false;
		}

		if (drmModeMoveCursor(drm->fd,
			crtc->id, conn->cursor_x, conn->cursor_y) != 0) {
			wlr_log_errno(WLR_ERROR, "%s: failed to move cursor",
				conn->output.name);
			return false;
		}
	} else {
		if (drmModeSetCursor(drm->fd, crtc->id, 0, 0, 0)) {
			wlr_log_errno(WLR_DEBUG, "%s: failed to unset hardware cursor",
				conn->output.name);
			return false;
		}
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

static void fill_empty_gamma_table(size_t size,
		uint16_t *r, uint16_t *g, uint16_t *b) {
	assert(0xFFFF < UINT64_MAX / (size - 1));
	for (uint32_t i = 0; i < size; ++i) {
		uint16_t val = (uint64_t)0xFFFF * i / (size - 1);
		r[i] = g[i] = b[i] = val;
	}
}

bool drm_legacy_crtc_set_gamma(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, size_t size, uint16_t *lut) {
	uint16_t *linear_lut = NULL;
	if (size == 0) {
		// The legacy interface doesn't offer a way to reset the gamma LUT
		size = drm_crtc_get_gamma_lut_size(drm, crtc);
		if (size == 0) {
			return false;
		}

		linear_lut = malloc(3 * size * sizeof(uint16_t));
		if (linear_lut == NULL) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}
		fill_empty_gamma_table(size, linear_lut, linear_lut + size,
			linear_lut + 2 * size);

		lut = linear_lut;
	}

	uint16_t *r = lut, *g = lut + size, *b = lut + 2 * size;
	if (drmModeCrtcSetGamma(drm->fd, crtc->id, size, r, g, b) != 0) {
		wlr_log_errno(WLR_ERROR, "Failed to set gamma LUT on CRTC %"PRIu32,
			crtc->id);
		return false;
	}

	free(linear_lut);
	return true;
}

const struct wlr_drm_interface legacy_iface = {
	.crtc_commit = legacy_crtc_commit,
};
