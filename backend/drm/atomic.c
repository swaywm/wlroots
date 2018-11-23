#include <math.h>
#include <stdlib.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

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

// Just a slightly modified wl_fixed_from_double
static uint32_t double_to_16_16_fixed(double d) {
	union {
		double d;
		int64_t i;
	} u = {
		.d = d + (3LL << (51 - 16)),
	};

	return u.i;
}

static double aspect_ratio(uint32_t width, uint32_t height) {
	return (double)width / (double)height;
}

static bool atomic_crtc_pageflip(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn,
		struct wlr_drm_crtc *crtc,
		uint32_t fb_id, drmModeModeInfo *mode) {
	uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;
	if (mode) {
		if (crtc->mode_id != 0) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->mode_id);
		}

		if (drmModeCreatePropertyBlob(drm->fd, mode, sizeof(*mode),
				&crtc->mode_id)) {
			wlr_log_errno(WLR_ERROR, "Unable to create property blob");
			return false;
		}

		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	} else {
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	}

	uint32_t conn_width = conn->crtc->primary->surf.width;
	uint32_t conn_height = conn->crtc->primary->surf.height;

	uint64_t src_x;
	uint64_t src_y;
	uint64_t src_w;
	uint64_t src_h;
	uint64_t crtc_x;
	uint64_t crtc_y;
	uint64_t crtc_w;
	uint64_t crtc_h;

	// TODO: Remove this hack when the old interface is removed
	if (conn->output.using_present) {
		struct wlr_image *img = conn->output.image;
		if (!img) {
			src_x = 0;
			src_y = 0;
			src_w = 0;
			src_h = 0;
		} else if (conn->output.viewport.src_x != -1.0) {
			src_x = double_to_16_16_fixed(conn->output.viewport.src_x);
			src_y = double_to_16_16_fixed(conn->output.viewport.src_y);
			src_w = double_to_16_16_fixed(conn->output.viewport.src_w);
			src_h = double_to_16_16_fixed(conn->output.viewport.src_h);
		} else {
			src_x = 0;
			src_y = 0;
			src_w = img->width << 16;
			src_h = img->height << 16;
		}
	} else {
		src_x = 0;
		src_y = 0;
		src_w = (uint64_t)conn_width << 16;
		src_h = (uint64_t)conn_height << 16;
	}

	if (conn->output.using_present) {
		struct wlr_image *img = conn->output.image;
		if (!img) {
			crtc_x = 0;
			crtc_y = 0;
			crtc_w = 0;
			crtc_h = 0;
		} else {
			uint32_t w;
			uint32_t h;

			if (conn->output.viewport.dest_w != -1) {
				w = conn->output.viewport.dest_w;
				h = conn->output.viewport.dest_h;
			} else {
				w = img->width;
				h = img->height;
			}

			bool flip = false;
			switch (conn->output.present_method) {
			case WLR_PRESENT_METHOD_ZOOM_CROP:
				flip = true;

				/* Fallthrough */
			case WLR_PRESENT_METHOD_ZOOM:;
				double r1 = aspect_ratio(conn_width, conn_height);
				double r2 = aspect_ratio(w, h);

				if (flip) {
					double tmp = r1;
					r1 = r2;
					r2 = tmp;
				}

				double scale;
				if (r2 < r1) {
					scale = (double)conn_height / h;
				} else {
					scale = (double)conn_width / w;
				}

				w = nearbyint(w * scale);
				h = nearbyint(h * scale);

				/* Fallthrough */
			case WLR_PRESENT_METHOD_CENTER:
				crtc_x = (uint64_t)conn_width / 2 - (uint64_t)w / 2;
				crtc_y = (uint64_t)conn_height / 2 - (uint64_t)h / 2;
				crtc_w = w;
				crtc_h = h;
				break;
			case WLR_PRESENT_METHOD_DEFAULT:
			case WLR_PRESENT_METHOD_STRETCH:
				crtc_x = 0;
				crtc_y = 0;
				crtc_w = w;
				crtc_h = h;
				break;
			default:
				abort();
			}
		}
	} else {
		crtc_x = 0;
		crtc_y = 0;
		crtc_w = conn_width;
		crtc_h = conn_height;
	}

	struct atomic atom;
	atomic_begin(crtc, &atom);

	uint32_t conn_id = conn->id;
	uint32_t crtc_id = crtc->id;
	uint32_t plane_id = crtc->primary->id;
	const union wlr_drm_connector_props *conn_props = &conn->props;
	const union wlr_drm_crtc_props *crtc_props = &crtc->props;
	const union wlr_drm_plane_props *plane_props = &crtc->primary->props;

	atomic_add(&atom, conn_id, conn_props->crtc_id, crtc_id);
	if (mode && conn_props->link_status != 0) {
		atomic_add(&atom, conn_id, conn_props->link_status,
			DRM_MODE_LINK_STATUS_GOOD);
	}

	atomic_add(&atom, crtc_id, crtc_props->mode_id, crtc->mode_id);
	atomic_add(&atom, crtc_id, crtc_props->active, 1);

	atomic_add(&atom, plane_id, plane_props->src_x, src_x);
	atomic_add(&atom, plane_id, plane_props->src_y, src_y);
	atomic_add(&atom, plane_id, plane_props->src_w, src_w);
	atomic_add(&atom, plane_id, plane_props->src_h, src_h);
	atomic_add(&atom, plane_id, plane_props->crtc_x, crtc_x);
	atomic_add(&atom, plane_id, plane_props->crtc_y, crtc_y);
	atomic_add(&atom, plane_id, plane_props->crtc_w, crtc_w);
	atomic_add(&atom, plane_id, plane_props->crtc_h, crtc_h);
	atomic_add(&atom, plane_id, plane_props->fb_id, fb_id);
	atomic_add(&atom, plane_id, plane_props->crtc_id, crtc_id);

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
		uint32_t fb_id = get_fb_for_bo(bo, plane->drm_format);
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
