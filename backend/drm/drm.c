#define _XOPEN_SOURCE 700
#include <assert.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <errno.h>
#include <gbm.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/cvt.h"
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"
#include "util/signal.h"

bool check_drm_features(struct wlr_drm_backend *drm) {
	uint64_t cap;
	if (drm->parent) {
		if (drmGetCap(drm->fd, DRM_CAP_PRIME, &cap) ||
				!(cap & DRM_PRIME_CAP_IMPORT)) {
			wlr_log(WLR_ERROR,
				"PRIME import not supported on secondary GPU");
			return false;
		}

		if (drmGetCap(drm->parent->fd, DRM_CAP_PRIME, &cap) ||
				!(cap & DRM_PRIME_CAP_EXPORT)) {
			wlr_log(WLR_ERROR,
				"PRIME export not supported on primary GPU");
			return false;
		}
	}

	if (drmSetClientCap(drm->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		wlr_log(WLR_ERROR, "DRM universal planes unsupported");
		return false;
	}

	if (drmGetCap(drm->fd, DRM_CAP_CRTC_IN_VBLANK_EVENT, &cap) || !cap) {
		wlr_log(WLR_ERROR, "DRM_CRTC_IN_VBLANK_EVENT unsupported");
		return false;
	}

	const char *no_atomic = getenv("WLR_DRM_NO_ATOMIC");
	if (no_atomic && strcmp(no_atomic, "1") == 0) {
		wlr_log(WLR_DEBUG,
			"WLR_DRM_NO_ATOMIC set, forcing legacy DRM interface");
		drm->iface = &legacy_iface;
	} else if (drmSetClientCap(drm->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		wlr_log(WLR_DEBUG,
			"Atomic modesetting unsupported, using legacy DRM interface");
		drm->iface = &legacy_iface;
	} else {
		wlr_log(WLR_DEBUG, "Using atomic DRM interface");
		drm->iface = &atomic_iface;
	}

	int ret = drmGetCap(drm->fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	drm->clock = (ret == 0 && cap == 1) ? CLOCK_MONOTONIC : CLOCK_REALTIME;

	ret = drmGetCap(drm->fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
	drm->addfb2_modifiers = ret == 0 && cap == 1;

	return true;
}

static bool add_plane(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, drmModePlane *drm_plane,
		uint32_t type, union wlr_drm_plane_props *props) {
	assert(!(type == DRM_PLANE_TYPE_PRIMARY && crtc->primary));

	if (type == DRM_PLANE_TYPE_CURSOR && crtc->cursor) {
		return true;
	}

	struct wlr_drm_plane *p = calloc(1, sizeof(*p));
	if (!p) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return false;
	}

	p->type = type;
	p->id = drm_plane->plane_id;
	p->props = *props;

	for (size_t j = 0; j < drm_plane->count_formats; ++j) {
		wlr_drm_format_set_add(&p->formats, drm_plane->formats[j],
			DRM_FORMAT_MOD_INVALID);
	}

	// Choose an RGB format for the plane
	uint32_t rgb_format = DRM_FORMAT_INVALID;
	for (size_t j = 0; j < drm_plane->count_formats; ++j) {
		uint32_t fmt = drm_plane->formats[j];

		if (fmt == DRM_FORMAT_ARGB8888) {
			// Prefer formats with alpha channel
			rgb_format = fmt;
			break;
		} else if (fmt == DRM_FORMAT_XRGB8888) {
			rgb_format = fmt;
		}
	}
	p->drm_format = rgb_format;

	if (p->props.in_formats) {
		uint64_t blob_id;
		if (!get_drm_prop(drm->fd, p->id, p->props.in_formats, &blob_id)) {
			wlr_log(WLR_ERROR, "Failed to read IN_FORMATS property");
			goto error;
		}

		drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(drm->fd, blob_id);
		if (!blob) {
			wlr_log(WLR_ERROR, "Failed to read IN_FORMATS blob");
			goto error;
		}

		struct drm_format_modifier_blob *data = blob->data;
		uint32_t *fmts = (uint32_t *)((char *)data + data->formats_offset);
		struct drm_format_modifier *mods = (struct drm_format_modifier *)
			((char *)data + data->modifiers_offset);
		for (uint32_t i = 0; i < data->count_modifiers; ++i) {
			for (int j = 0; j < 64; ++j) {
				if (mods[i].formats & ((uint64_t)1 << j)) {
					wlr_drm_format_set_add(&p->formats,
						fmts[j + mods[i].offset], mods[i].modifier);
				}
			}
		}

		drmModeFreePropertyBlob(blob);
	}

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		crtc->primary = p;
		break;
	case DRM_PLANE_TYPE_CURSOR:
		crtc->cursor = p;
		break;
	default:
		abort();
	}

	return true;

error:
	free(p);
	return false;
}

static bool init_planes(struct wlr_drm_backend *drm) {
	drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm->fd);
	if (!plane_res) {
		wlr_log_errno(WLR_ERROR, "Failed to get DRM plane resources");
		return false;
	}

	wlr_log(WLR_INFO, "Found %"PRIu32" DRM planes", plane_res->count_planes);

	for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
		uint32_t id = plane_res->planes[i];

		drmModePlane *plane = drmModeGetPlane(drm->fd, id);
		if (!plane) {
			wlr_log_errno(WLR_ERROR, "Failed to get DRM plane");
			goto error;
		}

		union wlr_drm_plane_props props = {0};
		if (!get_drm_plane_props(drm->fd, id, &props)) {
			drmModeFreePlane(plane);
			goto error;
		}

		uint64_t type;
		if (!get_drm_prop(drm->fd, id, props.type, &type)) {
			drmModeFreePlane(plane);
			goto error;
		}

		/*
		 * This is a very naive implementation of the plane matching
		 * logic. Primary and cursor planes should only work on a
		 * single CRTC, and this should be perfectly adequate, but
		 * overlay planes can potentially work with multiple CRTCs,
		 * meaning this could return inefficient/skewed results.
		 *
		 * However, we don't really care about overlay planes, as we
		 * don't support them yet. We only bother to keep basic
		 * tracking of them for DRM lease clients.
		 *
		 * possible_crtcs is a bitmask of crtcs, where each bit is an
		 * index into drmModeRes.crtcs. So if bit 0 is set (ffs starts
		 * counting from 1), crtc 0 is possible.
		 */
		int crtc_bit = ffs(plane->possible_crtcs) - 1;

		// This would be a kernel bug
		assert(crtc_bit >= 0 && (size_t)crtc_bit < drm->num_crtcs);

		struct wlr_drm_crtc *crtc = &drm->crtcs[crtc_bit];

		if (type == DRM_PLANE_TYPE_OVERLAY) {
			uint32_t *tmp = realloc(crtc->overlays,
				sizeof(*crtc->overlays) * (crtc->num_overlays + 1));
			if (tmp) {
				crtc->overlays = tmp;
				crtc->overlays[crtc->num_overlays++] = id;
			}
			drmModeFreePlane(plane);
			continue;
		}

		if (!add_plane(drm, crtc, plane, type, &props)) {
			drmModeFreePlane(plane);
			goto error;
		}

		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(plane_res);
	return true;

error:
	drmModeFreePlaneResources(plane_res);
	return false;
}

bool init_drm_resources(struct wlr_drm_backend *drm) {
	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(WLR_ERROR, "Failed to get DRM resources");
		return false;
	}

	wlr_log(WLR_INFO, "Found %d DRM CRTCs", res->count_crtcs);

	drm->num_crtcs = res->count_crtcs;
	if (drm->num_crtcs == 0) {
		drmModeFreeResources(res);
		return true;
	}

	drm->crtcs = calloc(drm->num_crtcs, sizeof(drm->crtcs[0]));
	if (!drm->crtcs) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];
		crtc->id = res->crtcs[i];
		crtc->legacy_crtc = drmModeGetCrtc(drm->fd, crtc->id);
		get_drm_crtc_props(drm->fd, crtc->id, &crtc->props);
	}

	if (!init_planes(drm)) {
		goto error_crtcs;
	}

	drmModeFreeResources(res);

	return true;

error_crtcs:
	free(drm->crtcs);
error_res:
	drmModeFreeResources(res);
	return false;
}

void finish_drm_resources(struct wlr_drm_backend *drm) {
	if (!drm) {
		return;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];

		drmModeFreeCrtc(crtc->legacy_crtc);

		if (crtc->mode_id) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->mode_id);
		}
		if (crtc->gamma_lut) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_lut);
		}

		if (crtc->primary) {
			wlr_drm_format_set_finish(&crtc->primary->formats);
			free(crtc->primary);
		}
		if (crtc->cursor) {
			wlr_drm_format_set_finish(&crtc->cursor->formats);
			free(crtc->cursor);
		}
		free(crtc->overlays);
	}

	free(drm->crtcs);
}

static struct wlr_drm_connector *get_drm_connector_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_drm(wlr_output));
	return (struct wlr_drm_connector *)wlr_output;
}

static bool drm_connector_attach_render(struct wlr_output *output,
		int *buffer_age) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	return drm_surface_make_current(&conn->crtc->primary->surf, buffer_age);
}

static bool drm_crtc_commit(struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);
	struct wlr_drm_crtc *crtc = conn->crtc;
	bool ok = drm->iface->crtc_commit(drm, conn, flags);
	if (ok && !(flags & DRM_MODE_ATOMIC_TEST_ONLY)) {
		memcpy(&crtc->current, &crtc->pending, sizeof(struct wlr_drm_crtc_state));
		drm_fb_move(&crtc->primary->queued_fb, &crtc->primary->pending_fb);
		if (crtc->cursor != NULL) {
			drm_fb_move(&crtc->cursor->queued_fb, &crtc->cursor->pending_fb);
		}
	} else {
		memcpy(&crtc->pending, &crtc->current, sizeof(struct wlr_drm_crtc_state));
		drm_fb_clear(&crtc->primary->pending_fb);
		if (crtc->cursor != NULL) {
			drm_fb_clear(&crtc->cursor->pending_fb);
		}
	}
	crtc->pending_modeset = false;
	return ok;
}

static bool drm_crtc_page_flip(struct wlr_drm_connector *conn) {
	struct wlr_drm_crtc *crtc = conn->crtc;

	// wlr_drm_interface.crtc_commit will perform either a non-blocking
	// page-flip, either a blocking modeset. When performing a blocking modeset
	// we'll wait for all queued page-flips to complete, so we don't need this
	// safeguard.
	if (conn->pageflip_pending && !crtc->pending_modeset) {
		wlr_log(WLR_ERROR, "Failed to page-flip output '%s': "
			"a page-flip is already pending", conn->output.name);
		return false;
	}

	assert(crtc->pending.active);
	assert(plane_get_next_fb(crtc->primary)->type != WLR_DRM_FB_TYPE_NONE);
	if (!drm_crtc_commit(conn, DRM_MODE_PAGE_FLIP_EVENT)) {
		return false;
	}

	conn->pageflip_pending = true;

	// wlr_output's API guarantees that submitting a buffer will schedule a
	// frame event. However the DRM backend will also schedule a frame event
	// when performing a modeset. Set frame_pending to true so that
	// wlr_output_schedule_frame doesn't trigger a synthetic frame event.
	conn->output.frame_pending = true;
	return true;
}

static uint32_t strip_alpha_channel(uint32_t format) {
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return DRM_FORMAT_XRGB8888;
	default:
		return DRM_FORMAT_INVALID;
	}
}

static bool test_buffer(struct wlr_drm_connector *conn,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_output *output = &conn->output;
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);

	if (!drm->session->active) {
		return false;
	}

	/* Legacy never gets to have nice things. But I doubt this would ever work,
	 * and there is no reliable way to try, without risking messing up the
	 * modesetting state. */
	if (drm->iface == &legacy_iface) {
		return false;
	}

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}

	struct wlr_dmabuf_attributes attribs;
	if (!wlr_buffer_get_dmabuf(wlr_buffer, &attribs)) {
		return false;
	}

	if (attribs.flags != 0) {
		return false;
	}

	if (!wlr_drm_format_set_has(&crtc->primary->formats,
			attribs.format, attribs.modifier)) {
		// The format isn't supported by the plane. Try stripping the alpha
		// channel, if any.
		uint32_t format = strip_alpha_channel(attribs.format);
		if (format != DRM_FORMAT_INVALID && wlr_drm_format_set_has(
				&crtc->primary->formats, format, attribs.modifier)) {
			attribs.format = format;
		} else {
			return false;
		}
	}

	return true;
}

static bool drm_connector_test(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);

	if ((output->pending.committed & WLR_OUTPUT_STATE_ENABLED) &&
			output->pending.enabled) {
		if (output->current_mode == NULL &&
				!(output->pending.committed & WLR_OUTPUT_STATE_MODE)) {
			wlr_log(WLR_DEBUG, "Can't enable an output without a mode");
			return false;
		}
	}

	if ((output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
			output->pending.buffer_type == WLR_OUTPUT_STATE_BUFFER_SCANOUT) {
		if (!test_buffer(conn, output->pending.buffer)) {
			return false;
		}
	}

	return true;
}

static struct wlr_output_mode *drm_connector_get_pending_mode(
		struct wlr_drm_connector *conn) {
	struct wlr_output *output = &conn->output;

	switch (output->pending.mode_type) {
	case WLR_OUTPUT_STATE_MODE_FIXED:
		return output->pending.mode;
	case WLR_OUTPUT_STATE_MODE_CUSTOM:;
		drmModeModeInfo mode = {0};
		generate_cvt_mode(&mode, output->pending.custom_mode.width,
			output->pending.custom_mode.height,
			(float)output->pending.custom_mode.refresh / 1000, false, false);
		mode.type = DRM_MODE_TYPE_USERDEF;
		return wlr_drm_connector_add_mode(output, &mode);
	}
	abort();
}

static bool drm_connector_commit_buffer(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = crtc->primary;

	assert(output->pending.committed & WLR_OUTPUT_STATE_BUFFER);
	switch (output->pending.buffer_type) {
	case WLR_OUTPUT_STATE_BUFFER_RENDER:
		if (!drm_fb_lock_surface(&plane->pending_fb, &plane->surf)) {
			wlr_log(WLR_ERROR, "drm_fb_lock_surface failed");
			return false;
		}
		break;
	case WLR_OUTPUT_STATE_BUFFER_SCANOUT:;
		struct wlr_buffer *buffer = output->pending.buffer;
		if (!test_buffer(conn, output->pending.buffer)) {
			return false;
		}
		if (!drm_fb_import_wlr(&plane->pending_fb, &drm->renderer, buffer,
				&crtc->primary->formats)) {
			return false;
		}
		break;
	}

	if (!drm_crtc_page_flip(conn)) {
		return false;
	}

	return true;
}

bool drm_connector_supports_vrr(struct wlr_drm_connector *conn) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}

	uint64_t vrr_capable;
	if (conn->props.vrr_capable == 0 ||
			!get_drm_prop(drm->fd, conn->id, conn->props.vrr_capable,
			&vrr_capable) || !vrr_capable) {
		wlr_log(WLR_DEBUG, "Failed to enable adaptive sync: "
			"connector '%s' doesn't support VRR", conn->output.name);
		return false;
	}

	if (crtc->props.vrr_enabled == 0) {
		wlr_log(WLR_DEBUG, "Failed to enable adaptive sync: "
			"CRTC %"PRIu32" doesn't support VRR", crtc->id);
		return false;
	}

	return true;
}

static bool drm_connector_commit(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);

	if (!drm_connector_test(output)) {
		return false;
	}

	if (!drm->session->active) {
		return false;
	}

	if (output->pending.committed &
			(WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED)) {
		struct wlr_output_mode *wlr_mode = output->current_mode;

		bool enable = (output->pending.committed & WLR_OUTPUT_STATE_ENABLED) ?
			output->pending.enabled : output->enabled;
		if (!enable) {
			wlr_mode = NULL;
		}

		if (output->pending.committed & WLR_OUTPUT_STATE_MODE) {
			assert(enable);
			wlr_mode = drm_connector_get_pending_mode(conn);
			if (wlr_mode == NULL) {
				return false;
			}
		}

		if (!drm_connector_set_mode(conn, wlr_mode)) {
			return false;
		}
	} else if (output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		// TODO: support modesetting with a buffer
		if (!drm_connector_commit_buffer(output)) {
			return false;
		}
	} else if (output->pending.committed &
			(WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED |
			WLR_OUTPUT_STATE_GAMMA_LUT)) {
		assert(conn->crtc != NULL);
		// TODO: maybe request a page-flip event here?
		if (!drm_crtc_commit(conn, 0)) {
			return false;
		}
	}

	return true;
}

static void drm_connector_rollback_render(struct wlr_output *output) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	wlr_egl_unset_current(&drm->renderer.egl);
}

size_t drm_crtc_get_gamma_lut_size(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	if (crtc->props.gamma_lut_size == 0) {
		return (size_t)crtc->legacy_crtc->gamma_size;
	}

	uint64_t gamma_lut_size;
	if (!get_drm_prop(drm->fd, crtc->id, crtc->props.gamma_lut_size,
			&gamma_lut_size)) {
		wlr_log(WLR_ERROR, "Unable to get gamma lut size");
		return 0;
	}

	return gamma_lut_size;
}

static size_t drm_connector_get_gamma_size(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (crtc == NULL) {
		return 0;
	}

	return drm_crtc_get_gamma_lut_size(drm, crtc);
}

static bool drm_connector_export_dmabuf(struct wlr_output *output,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (!drm->session->active) {
		return false;
	}

	if (!crtc) {
		return false;
	}

	struct wlr_drm_fb *fb = &crtc->primary->queued_fb;
	if (fb->type == WLR_DRM_FB_TYPE_NONE) {
		fb = &crtc->primary->current_fb;
	}
	if (fb->type == WLR_DRM_FB_TYPE_NONE) {
		return false;
	}

	return export_drm_bo(fb->bo, attribs);
}

struct wlr_drm_fb *plane_get_next_fb(struct wlr_drm_plane *plane) {
	if (plane->pending_fb.type != WLR_DRM_FB_TYPE_NONE) {
		return &plane->pending_fb;
	}
	if (plane->queued_fb.type != WLR_DRM_FB_TYPE_NONE) {
		return &plane->queued_fb;
	}
	return &plane->current_fb;
}

static bool drm_connector_pageflip_renderer(struct wlr_drm_connector *conn) {
	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		wlr_log(WLR_ERROR, "Page-flip failed on connector '%s': no CRTC",
			conn->output.name);
		return false;
	}

	// drm_crtc_page_flip expects a FB to be available
	struct wlr_drm_plane *plane = crtc->primary;
	if (plane_get_next_fb(plane)->type == WLR_DRM_FB_TYPE_NONE) {
		drm_surface_render_black_frame(&plane->surf);
		if (!drm_fb_lock_surface(&plane->pending_fb, &plane->surf)) {
			return false;
		}
	}

	return drm_crtc_page_flip(conn);
}

static bool drm_connector_init_renderer(struct wlr_drm_connector *conn,
		struct wlr_drm_mode *mode) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);

	if (conn->state != WLR_DRM_CONN_CONNECTED &&
			conn->state != WLR_DRM_CONN_NEEDS_MODESET) {
		return false;
	}

	wlr_log(WLR_DEBUG, "Initializing renderer on connector '%s'",
		conn->output.name);

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		wlr_log(WLR_ERROR, "Failed to initialize renderer on connector '%s': "
			"no CRTC", conn->output.name);
		return false;
	}
	struct wlr_drm_plane *plane = crtc->primary;

	crtc->pending_modeset = true;
	crtc->pending.active = true;
	crtc->pending.mode = mode;

	int width = mode->wlr_mode.width;
	int height = mode->wlr_mode.height;
	uint32_t format = drm->renderer.gbm_format;

	bool modifiers = true;
	const char *no_modifiers = getenv("WLR_DRM_NO_MODIFIERS");
	if (no_modifiers != NULL && strcmp(no_modifiers, "1") == 0) {
		wlr_log(WLR_DEBUG,
			"WLR_DRM_NO_MODIFIERS set, initializing planes without modifiers");
		modifiers = false;
	}

	if (!drm_plane_init_surface(plane, drm, width, height, format, 0, modifiers) ||
			!drm_connector_pageflip_renderer(conn)) {
		if (!modifiers) {
			wlr_log(WLR_ERROR, "Failed to initialize renderer "
				"on connector '%s': initial page-flip failed",
				conn->output.name);
			return false;
		}

		// If page-flipping with modifiers enabled doesn't work, retry without
		// modifiers
		wlr_log(WLR_INFO, "Page-flip failed with primary FB modifiers enabled, "
			"retrying without modifiers");
		modifiers = false;

		crtc->pending_modeset = true;
		crtc->pending.active = true;
		crtc->pending.mode = mode;

		if (!drm_plane_init_surface(plane, drm, width, height, format,
				0, modifiers)) {
			return false;
		}
		if (!drm_connector_pageflip_renderer(conn)) {
			wlr_log(WLR_ERROR, "Failed to initialize renderer "
				"on connector '%s': initial page-flip failed",
				conn->output.name);
			return false;
		}
	}

	return true;
}

static void realloc_crtcs(struct wlr_drm_backend *drm);

static void attempt_enable_needs_modeset(struct wlr_drm_backend *drm) {
	// Try to modeset any output that has a desired mode and a CRTC (ie. was
	// lacking a CRTC on last modeset)
	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		if (conn->state == WLR_DRM_CONN_NEEDS_MODESET &&
				conn->crtc != NULL && conn->desired_mode != NULL &&
				conn->desired_enabled) {
			wlr_log(WLR_DEBUG, "Output %s has a desired mode and a CRTC, "
				"attempting a modeset", conn->output.name);
			drm_connector_set_mode(conn, conn->desired_mode);
		}
	}
}

static void drm_connector_cleanup(struct wlr_drm_connector *conn);

bool drm_connector_set_mode(struct wlr_drm_connector *conn,
		struct wlr_output_mode *wlr_mode) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);

	conn->desired_enabled = wlr_mode != NULL;
	conn->desired_mode = wlr_mode;

	if (wlr_mode == NULL) {
		if (conn->crtc != NULL) {
			conn->crtc->pending_modeset = true;
			conn->crtc->pending.active = false;
			if (!drm_crtc_commit(conn, 0)) {
				return false;
			}
			realloc_crtcs(drm);
			attempt_enable_needs_modeset(drm);
		}
		wlr_output_update_enabled(&conn->output, false);
		return true;
	}

	if (conn->state != WLR_DRM_CONN_CONNECTED
			&& conn->state != WLR_DRM_CONN_NEEDS_MODESET) {
		wlr_log(WLR_ERROR, "Cannot modeset a disconnected output");
		return false;
	}

	if (conn->crtc == NULL) {
		// Maybe we can steal a CRTC from a disabled output
		realloc_crtcs(drm);
	}
	if (conn->crtc == NULL) {
		wlr_log(WLR_ERROR, "Cannot modeset '%s': no CRTC for this connector",
			conn->output.name);
		return false;
	}

	wlr_log(WLR_INFO, "Modesetting '%s' with '%" PRId32 "x%" PRId32 "@%" PRId32 "mHz'",
		conn->output.name, wlr_mode->width, wlr_mode->height,
		wlr_mode->refresh);

	struct wlr_drm_mode *mode = (struct wlr_drm_mode *)wlr_mode;
	if (!drm_connector_init_renderer(conn, mode)) {
		wlr_log(WLR_ERROR, "Failed to initialize renderer for plane");
		return false;
	}

	conn->state = WLR_DRM_CONN_CONNECTED;
	conn->desired_mode = NULL;
	wlr_output_update_mode(&conn->output, wlr_mode);
	wlr_output_update_enabled(&conn->output, true);
	conn->desired_enabled = true;

	// When switching VTs, the mode is not updated but the buffers become
	// invalid, so we need to manually damage the output here
	wlr_output_damage_whole(&conn->output);

	return true;
}

struct wlr_output_mode *wlr_drm_connector_add_mode(struct wlr_output *output,
		const drmModeModeInfo *modeinfo) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);

	if (modeinfo->type != DRM_MODE_TYPE_USERDEF) {
		return NULL;
	}

	struct wlr_output_mode *wlr_mode;
	wl_list_for_each(wlr_mode, &conn->output.modes, link) {
		struct wlr_drm_mode *mode = (struct wlr_drm_mode *)wlr_mode;
		if (memcmp(&mode->drm_mode, modeinfo, sizeof(*modeinfo)) == 0) {
			return wlr_mode;
		}
	}

	struct wlr_drm_mode *mode = calloc(1, sizeof(*mode));
	if (!mode) {
		return NULL;
	}
	memcpy(&mode->drm_mode, modeinfo, sizeof(*modeinfo));

	mode->wlr_mode.width = mode->drm_mode.hdisplay;
	mode->wlr_mode.height = mode->drm_mode.vdisplay;
	mode->wlr_mode.refresh = calculate_refresh_rate(modeinfo);

	wlr_log(WLR_INFO, "Registered custom mode "
			"%"PRId32"x%"PRId32"@%"PRId32,
			mode->wlr_mode.width, mode->wlr_mode.height,
			mode->wlr_mode.refresh);
	wl_list_insert(&conn->output.modes, &mode->wlr_mode.link);

	return &mode->wlr_mode;
}

static bool drm_connector_set_cursor(struct wlr_output *output,
		struct wlr_texture *texture, float scale,
		enum wl_output_transform transform,
		int32_t hotspot_x, int32_t hotspot_y, bool update_texture) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (!crtc) {
		return false;
	}

	struct wlr_drm_plane *plane = crtc->cursor;
	if (plane == NULL) {
		return false;
	}

	if (!plane->surf.gbm) {
		int ret;
		uint64_t w, h;
		ret = drmGetCap(drm->fd, DRM_CAP_CURSOR_WIDTH, &w);
		w = ret ? 64 : w;
		ret = drmGetCap(drm->fd, DRM_CAP_CURSOR_HEIGHT, &h);
		h = ret ? 64 : h;

		if (!drm_plane_init_surface(plane, drm, w, h,
				DRM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR, false)) {
			wlr_log(WLR_ERROR, "Cannot allocate cursor resources");
			return false;
		}
	}

	float hotspot_proj[9];
	wlr_matrix_projection(hotspot_proj, plane->surf.width,
		plane->surf.height, output->transform);

	struct wlr_box hotspot = { .x = hotspot_x, .y = hotspot_y };
	wlr_box_transform(&hotspot, &hotspot,
		wlr_output_transform_invert(output->transform),
		plane->surf.width, plane->surf.height);

	if (plane->cursor_hotspot_x != hotspot.x ||
			plane->cursor_hotspot_y != hotspot.y) {
		// Update cursor hotspot
		conn->cursor_x -= hotspot.x - plane->cursor_hotspot_x;
		conn->cursor_y -= hotspot.y - plane->cursor_hotspot_y;
		plane->cursor_hotspot_x = hotspot.x;
		plane->cursor_hotspot_y = hotspot.y;

		wlr_output_update_needs_frame(output);
	}

	if (!update_texture) {
		// Don't update cursor image
		return true;
	}

	plane->cursor_enabled = false;
	if (texture != NULL) {
		int width, height;
		wlr_texture_get_size(texture, &width, &height);
		width = width * output->scale / scale;
		height = height * output->scale / scale;

		if (width > (int)plane->surf.width || height > (int)plane->surf.height) {
			wlr_log(WLR_ERROR, "Cursor too large (max %dx%d)",
				(int)plane->surf.width, (int)plane->surf.height);
			return false;
		}

		if (!drm_surface_make_current(&plane->surf, NULL)) {
			return false;
		}

		struct wlr_renderer *rend = plane->surf.renderer->wlr_rend;

		struct wlr_box cursor_box = { .width = width, .height = height };

		float matrix[9];
		wlr_matrix_project_box(matrix, &cursor_box, transform, 0, hotspot_proj);

		wlr_renderer_begin(rend, plane->surf.width, plane->surf.height);
		wlr_renderer_clear(rend, (float[]){ 0.0, 0.0, 0.0, 0.0 });
		wlr_render_texture_with_matrix(rend, texture, matrix, 1.0);
		wlr_renderer_end(rend);

		if (!drm_fb_lock_surface(&plane->pending_fb, &plane->surf)) {
			return false;
		}

		plane->cursor_enabled = true;
	}

	if (plane->cursor_enabled) {
		drm_fb_acquire(&plane->pending_fb, drm, &plane->mgpu_surf);
		/* Workaround for nouveau buffers created with GBM_BO_USER_LINEAR are
		 * placed in NOUVEAU_GEM_DOMAIN_GART. When the bo is attached to the
		 * cursor plane it is moved to NOUVEAU_GEM_DOMAIN_VRAM. However, this
		 * does not wait for the render operations to complete, leaving an
		 * empty surface. See
		 * https://gitlab.freedesktop.org/xorg/driver/xf86-video-nouveau/issues/480
		 * The render operations can be waited for using:
		 */
		glFinish();
	}

	wlr_output_update_needs_frame(output);
	return true;
}

static bool drm_connector_move_cursor(struct wlr_output *output,
		int x, int y) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	if (!conn->crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = conn->crtc->cursor;
	if (!plane) {
		return false;
	}

	struct wlr_box box = { .x = x, .y = y };

	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);
	wlr_box_transform(&box, &box, transform, width, height);

	if (plane != NULL) {
		box.x -= plane->cursor_hotspot_x;
		box.y -= plane->cursor_hotspot_y;
	}

	conn->cursor_x = box.x;
	conn->cursor_y = box.y;

	wlr_output_update_needs_frame(output);
	return true;
}

bool drm_connector_is_cursor_visible(struct wlr_drm_connector *conn) {
	assert(conn->crtc != NULL && conn->crtc->cursor != NULL);
	struct wlr_drm_plane *plane = conn->crtc->cursor;

	return plane->cursor_enabled &&
		conn->cursor_x < conn->output.width &&
		conn->cursor_y < conn->output.height &&
		conn->cursor_x + (int)plane->surf.width >= 0 &&
		conn->cursor_y + (int)plane->surf.height >= 0;
}

static void drm_connector_destroy(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	drm_connector_cleanup(conn);
	drmModeFreeCrtc(conn->old_crtc);
	wl_list_remove(&conn->link);
	free(conn);
}

static const struct wlr_output_impl output_impl = {
	.set_cursor = drm_connector_set_cursor,
	.move_cursor = drm_connector_move_cursor,
	.destroy = drm_connector_destroy,
	.attach_render = drm_connector_attach_render,
	.test = drm_connector_test,
	.commit = drm_connector_commit,
	.rollback_render = drm_connector_rollback_render,
	.get_gamma_size = drm_connector_get_gamma_size,
	.export_dmabuf = drm_connector_export_dmabuf,
};

bool wlr_output_is_drm(struct wlr_output *output) {
	return output->impl == &output_impl;
}

static const int32_t subpixel_map[] = {
	[DRM_MODE_SUBPIXEL_UNKNOWN] = WL_OUTPUT_SUBPIXEL_UNKNOWN,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_RGB] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_BGR] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR,
	[DRM_MODE_SUBPIXEL_VERTICAL_RGB] = WL_OUTPUT_SUBPIXEL_VERTICAL_RGB,
	[DRM_MODE_SUBPIXEL_VERTICAL_BGR] = WL_OUTPUT_SUBPIXEL_VERTICAL_BGR,
	[DRM_MODE_SUBPIXEL_NONE] = WL_OUTPUT_SUBPIXEL_NONE,
};

static void dealloc_crtc(struct wlr_drm_connector *conn) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);
	if (conn->crtc == NULL) {
		return;
	}

	wlr_log(WLR_DEBUG, "De-allocating CRTC %zu for output '%s'",
		conn->crtc - drm->crtcs, conn->output.name);

	conn->crtc->pending_modeset = true;
	conn->crtc->pending.active = false;
	if (!drm_crtc_commit(conn, 0)) {
		return;
	}

	drm_plane_finish_surface(conn->crtc->primary);
	drm_plane_finish_surface(conn->crtc->cursor);
	if (conn->crtc->cursor != NULL) {
		conn->crtc->cursor->cursor_enabled = false;
	}

	conn->crtc = NULL;
}

static void realloc_crtcs(struct wlr_drm_backend *drm) {
	assert(drm->num_crtcs > 0);

	size_t num_outputs = wl_list_length(&drm->outputs);
	if (num_outputs == 0) {
		return;
	}

	wlr_log(WLR_DEBUG, "Reallocating CRTCs");

	struct wlr_drm_connector *connectors[num_outputs];
	uint32_t connector_constraints[num_outputs];
	uint32_t previous_match[drm->num_crtcs];
	uint32_t new_match[drm->num_crtcs];

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		previous_match[i] = UNMATCHED;
	}

	wlr_log(WLR_DEBUG, "State before reallocation:");
	size_t i = 0;
	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		connectors[i] = conn;

		wlr_log(WLR_DEBUG, "  '%s' crtc=%d state=%d desired_enabled=%d",
			conn->output.name,
			conn->crtc ? (int)(conn->crtc - drm->crtcs) : -1,
			conn->state, conn->desired_enabled);

		if (conn->crtc) {
			previous_match[conn->crtc - drm->crtcs] = i;
		}

		// Only search CRTCs for user-enabled outputs (that are already
		// connected or in need of a modeset)
		if ((conn->state == WLR_DRM_CONN_CONNECTED ||
				conn->state == WLR_DRM_CONN_NEEDS_MODESET) &&
				conn->desired_enabled) {
			connector_constraints[i] = conn->possible_crtc;
		} else {
			// Will always fail to match anything
			connector_constraints[i] = 0;
		}

		++i;
	}

	match_obj(num_outputs, connector_constraints,
		drm->num_crtcs, previous_match, new_match);

	// Converts our crtc=>connector result into a connector=>crtc one.
	ssize_t connector_match[num_outputs];
	for (size_t i = 0 ; i < num_outputs; ++i) {
		connector_match[i] = -1;
	}
	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (new_match[i] != UNMATCHED) {
			connector_match[new_match[i]] = i;
		}
	}

	/*
	 * In the case that we add a new connector (hotplug) and we fail to
	 * match everything, we prefer to fail the new connector and keep all
	 * of the old mappings instead.
	 */
	for (size_t i = 0; i < num_outputs; ++i) {
		struct wlr_drm_connector *conn = connectors[i];
		if (conn->state == WLR_DRM_CONN_CONNECTED &&
				conn->desired_enabled &&
				connector_match[i] == -1) {
			wlr_log(WLR_DEBUG, "Could not match a CRTC for previously connected output; "
					"keeping old configuration");
			return;
		}
	}
	wlr_log(WLR_DEBUG, "State after reallocation:");

	// Apply new configuration
	for (size_t i = 0; i < num_outputs; ++i) {
		struct wlr_drm_connector *conn = connectors[i];
		bool prev_enabled = conn->crtc;

		wlr_log(WLR_DEBUG, "  '%s' crtc=%zd state=%d desired_enabled=%d",
			conn->output.name,
			connector_match[i],
			conn->state, conn->desired_enabled);

		// We don't need to change anything.
		if (prev_enabled && connector_match[i] == conn->crtc - drm->crtcs) {
			continue;
		}

		dealloc_crtc(conn);

		if (connector_match[i] == -1) {
			if (prev_enabled) {
				wlr_log(WLR_DEBUG, "Output has %s lost its CRTC",
					conn->output.name);
				conn->state = WLR_DRM_CONN_NEEDS_MODESET;
				wlr_output_update_enabled(&conn->output, false);
				conn->desired_mode = conn->output.current_mode;
				wlr_output_update_mode(&conn->output, NULL);
			}
			continue;
		}

		conn->crtc = &drm->crtcs[connector_match[i]];

		// Only realloc buffers if we have actually been modeset
		if (conn->state != WLR_DRM_CONN_CONNECTED) {
			continue;
		}

		struct wlr_drm_mode *mode =
			(struct wlr_drm_mode *)conn->output.current_mode;
		if (!drm_connector_init_renderer(conn, mode)) {
			wlr_log(WLR_ERROR, "Failed to initialize renderer on output %s",
				conn->output.name);
			wlr_output_update_enabled(&conn->output, false);
			continue;
		}

		wlr_output_damage_whole(&conn->output);
	}
}

static uint32_t get_possible_crtcs(int fd, drmModeRes *res,
		drmModeConnector *conn, bool is_mst) {
	uint32_t ret = 0;

	for (int i = 0; i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc) {
			continue;
		}

		ret |= enc->possible_crtcs;

		drmModeFreeEncoder(enc);
	}

	// Sometimes DP MST connectors report no encoders, so we'll loop though
	// all of the encoders of the MST type instead.
	// TODO: See if there is a better solution.

	if (!is_mst || ret) {
		return ret;
	}

	for (int i = 0; i < res->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, res->encoders[i]);
		if (!enc) {
			continue;
		}

		if (enc->encoder_type == DRM_MODE_ENCODER_DPMST) {
			ret |= enc->possible_crtcs;
		}

		drmModeFreeEncoder(enc);
	}

	return ret;
}

void scan_drm_connectors(struct wlr_drm_backend *drm) {
	/*
	 * This GPU is not really a modesetting device.
	 * It's just being used as a renderer.
	 */
	if (drm->num_crtcs == 0) {
		return;
	}

	wlr_log(WLR_INFO, "Scanning DRM connectors");

	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(WLR_ERROR, "Failed to get DRM resources");
		return;
	}

	size_t seen_len = wl_list_length(&drm->outputs);
	// +1 so length can never be 0, which is undefined behaviour.
	// Last element isn't used.
	bool seen[seen_len + 1];
	memset(seen, false, sizeof(seen));
	size_t new_outputs_len = 0;
	struct wlr_drm_connector *new_outputs[res->count_connectors + 1];

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *drm_conn = drmModeGetConnector(drm->fd,
			res->connectors[i]);
		if (!drm_conn) {
			wlr_log_errno(WLR_ERROR, "Failed to get DRM connector");
			continue;
		}
		drmModeEncoder *curr_enc = drmModeGetEncoder(drm->fd,
			drm_conn->encoder_id);

		ssize_t index = -1;
		struct wlr_drm_connector *c, *wlr_conn = NULL;
		wl_list_for_each(c, &drm->outputs, link) {
			index++;
			if (c->id == drm_conn->connector_id) {
				wlr_conn = c;
				break;
			}
		}

		if (!wlr_conn) {
			wlr_conn = calloc(1, sizeof(*wlr_conn));
			if (!wlr_conn) {
				wlr_log_errno(WLR_ERROR, "Allocation failed");
				drmModeFreeEncoder(curr_enc);
				drmModeFreeConnector(drm_conn);
				continue;
			}
			wlr_output_init(&wlr_conn->output, &drm->backend, &output_impl,
				drm->display);

			wlr_conn->state = WLR_DRM_CONN_DISCONNECTED;
			wlr_conn->id = drm_conn->connector_id;

			snprintf(wlr_conn->output.name, sizeof(wlr_conn->output.name),
				"%s-%"PRIu32, conn_get_name(drm_conn->connector_type),
				drm_conn->connector_type_id);

			if (curr_enc) {
				wlr_conn->old_crtc = drmModeGetCrtc(drm->fd, curr_enc->crtc_id);
			}

			wl_list_insert(drm->outputs.prev, &wlr_conn->link);
			wlr_log(WLR_INFO, "Found connector '%s'", wlr_conn->output.name);
		} else {
			seen[index] = true;
		}

		if (curr_enc) {
			for (size_t i = 0; i < drm->num_crtcs; ++i) {
				if (drm->crtcs[i].id == curr_enc->crtc_id) {
					wlr_conn->crtc = &drm->crtcs[i];
					break;
				}
			}
		} else {
			wlr_conn->crtc = NULL;
		}

		// This can only happen *after* hotplug, since we haven't read the
		// connector properties yet
		if (wlr_conn->props.link_status != 0) {
			uint64_t link_status;
			if (!get_drm_prop(drm->fd, wlr_conn->id,
					wlr_conn->props.link_status, &link_status)) {
				wlr_log(WLR_ERROR, "Failed to get link status for '%s'",
					wlr_conn->output.name);
				continue;
			}

			if (link_status == DRM_MODE_LINK_STATUS_BAD) {
				// We need to reload our list of modes and force a modeset
				wlr_log(WLR_INFO, "Bad link for '%s'", wlr_conn->output.name);
				drm_connector_cleanup(wlr_conn);
			}
		}

		if (wlr_conn->state == WLR_DRM_CONN_DISCONNECTED &&
				drm_conn->connection == DRM_MODE_CONNECTED) {
			wlr_log(WLR_INFO, "'%s' connected", wlr_conn->output.name);
			wlr_log(WLR_DEBUG, "Current CRTC: %d",
				wlr_conn->crtc ? (int)wlr_conn->crtc->id : -1);

			wlr_conn->output.phys_width = drm_conn->mmWidth;
			wlr_conn->output.phys_height = drm_conn->mmHeight;
			wlr_log(WLR_INFO, "Physical size: %"PRId32"x%"PRId32,
				wlr_conn->output.phys_width, wlr_conn->output.phys_height);
			wlr_conn->output.subpixel = subpixel_map[drm_conn->subpixel];

			get_drm_connector_props(drm->fd, wlr_conn->id, &wlr_conn->props);

			size_t edid_len = 0;
			uint8_t *edid = get_drm_prop_blob(drm->fd,
				wlr_conn->id, wlr_conn->props.edid, &edid_len);
			parse_edid(&wlr_conn->output, edid_len, edid);
			free(edid);

			struct wlr_output *output = &wlr_conn->output;
			char description[128];
			snprintf(description, sizeof(description), "%s %s %s (%s)",
				output->make, output->model, output->serial, output->name);
			wlr_output_set_description(output, description);

			wlr_log(WLR_INFO, "Detected modes:");

			for (int i = 0; i < drm_conn->count_modes; ++i) {
				struct wlr_drm_mode *mode = calloc(1, sizeof(*mode));
				if (!mode) {
					wlr_log_errno(WLR_ERROR, "Allocation failed");
					continue;
				}

				if (drm_conn->modes[i].flags & DRM_MODE_FLAG_INTERLACE) {
					free(mode);
					continue;
				}

				mode->drm_mode = drm_conn->modes[i];
				mode->wlr_mode.width = mode->drm_mode.hdisplay;
				mode->wlr_mode.height = mode->drm_mode.vdisplay;
				mode->wlr_mode.refresh = calculate_refresh_rate(&mode->drm_mode);
				if (mode->drm_mode.type & DRM_MODE_TYPE_PREFERRED) {
					mode->wlr_mode.preferred = true;
				}

				wlr_log(WLR_INFO, "  %"PRId32"x%"PRId32"@%"PRId32" %s",
					mode->wlr_mode.width, mode->wlr_mode.height,
					mode->wlr_mode.refresh,
					mode->wlr_mode.preferred ? "(preferred)" : "");

				wl_list_insert(&wlr_conn->output.modes, &mode->wlr_mode.link);
			}

			size_t path_len;
			bool is_mst = false;
			char *path = get_drm_prop_blob(drm->fd, wlr_conn->id,
				wlr_conn->props.path, &path_len);
			if (path && path_len > 4 && strncmp(path, "mst:", 4) == 0) {
				is_mst = true;
			}
			free(path);

			wlr_conn->possible_crtc = get_possible_crtcs(drm->fd, res, drm_conn,
				is_mst);
			if (wlr_conn->possible_crtc == 0) {
				wlr_log(WLR_ERROR, "No CRTC possible for connector '%s'",
					wlr_conn->output.name);
			}

			// TODO: this results in connectors being enabled without a mode
			// set
			wlr_output_update_enabled(&wlr_conn->output, wlr_conn->crtc != NULL);
			wlr_conn->desired_enabled = true;

			wlr_conn->state = WLR_DRM_CONN_NEEDS_MODESET;
			new_outputs[new_outputs_len++] = wlr_conn;
		} else if ((wlr_conn->state == WLR_DRM_CONN_CONNECTED ||
				wlr_conn->state == WLR_DRM_CONN_NEEDS_MODESET) &&
				drm_conn->connection != DRM_MODE_CONNECTED) {
			wlr_log(WLR_INFO, "'%s' disconnected", wlr_conn->output.name);

			drm_connector_cleanup(wlr_conn);
		}

		drmModeFreeEncoder(curr_enc);
		drmModeFreeConnector(drm_conn);
	}

	drmModeFreeResources(res);

	// Iterate in reverse order because we'll remove items from the list and
	// still want indices to remain correct.
	struct wlr_drm_connector *conn, *tmp_conn;
	size_t index = wl_list_length(&drm->outputs);
	wl_list_for_each_reverse_safe(conn, tmp_conn, &drm->outputs, link) {
		index--;
		if (index >= seen_len || seen[index]) {
			continue;
		}

		wlr_log(WLR_INFO, "'%s' disappeared", conn->output.name);
		drm_connector_cleanup(conn);

		wlr_output_destroy(&conn->output);
	}

	realloc_crtcs(drm);

	for (size_t i = 0; i < new_outputs_len; ++i) {
		struct wlr_drm_connector *conn = new_outputs[i];

		wlr_log(WLR_INFO, "Requesting modeset for '%s'",
			conn->output.name);
		wlr_signal_emit_safe(&drm->backend.events.new_output,
			&conn->output);
	}

	attempt_enable_needs_modeset(drm);
}

static int mhz_to_nsec(int mhz) {
	return 1000000000000LL / mhz;
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, unsigned crtc_id, void *data) {
	struct wlr_drm_backend *drm = data;

	struct wlr_drm_connector *conn = NULL;
	struct wlr_drm_connector *search;
	wl_list_for_each(search, &drm->outputs, link) {
		if (search->crtc && search->crtc->id == crtc_id) {
			conn = search;
			break;
		}
	}
	if (!conn) {
		wlr_log(WLR_DEBUG, "No connector for crtc_id %u", crtc_id);
		return;
	}

	conn->pageflip_pending = false;

	if (conn->state != WLR_DRM_CONN_CONNECTED || conn->crtc == NULL) {
		return;
	}

	struct wlr_drm_plane *plane = conn->crtc->primary;
	if (plane->queued_fb.type != WLR_DRM_FB_TYPE_NONE) {
		drm_fb_move(&plane->current_fb, &plane->queued_fb);
	}
	if (conn->crtc->cursor &&
			conn->crtc->cursor->queued_fb.type != WLR_DRM_FB_TYPE_NONE) {
		drm_fb_move(&conn->crtc->cursor->current_fb,
			&conn->crtc->cursor->queued_fb);
	}

	uint32_t present_flags = WLR_OUTPUT_PRESENT_VSYNC |
		WLR_OUTPUT_PRESENT_HW_CLOCK | WLR_OUTPUT_PRESENT_HW_COMPLETION;
	/* Don't report ZERO_COPY in multi-gpu situations, because we had to copy
	 * data between the GPUs, even if we were using the direct scanout
	 * interface.
	 */
	if (!drm->parent && plane->current_fb.type == WLR_DRM_FB_TYPE_WLR_BUFFER) {
		present_flags |= WLR_OUTPUT_PRESENT_ZERO_COPY;
	}

	struct timespec present_time = {
		.tv_sec = tv_sec,
		.tv_nsec = tv_usec * 1000,
	};
	struct wlr_output_event_present present_event = {
		/* The DRM backend guarantees that the presentation event will be for
		 * the last submitted frame. */
		.commit_seq = conn->output.commit_seq,
		.when = &present_time,
		.seq = seq,
		.refresh = mhz_to_nsec(conn->output.refresh),
		.flags = present_flags,
	};
	wlr_output_send_present(&conn->output, &present_event);

	if (drm->session->active && conn->output.enabled) {
		wlr_output_send_frame(&conn->output);
	}
}

int handle_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = 3,
		.page_flip_handler2 = page_flip_handler,
	};

	drmHandleEvent(fd, &event);
	return 1;
}

void restore_drm_outputs(struct wlr_drm_backend *drm) {
	uint64_t to_close = (UINT64_C(1) << wl_list_length(&drm->outputs)) - 1;

	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		if (conn->state == WLR_DRM_CONN_CONNECTED) {
			conn->state = WLR_DRM_CONN_CLEANUP;
		}
	}

	time_t timeout = time(NULL) + 5;

	while (to_close && time(NULL) < timeout) {
		handle_drm_event(drm->fd, 0, NULL);
		size_t i = 0;
		struct wlr_drm_connector *conn;
		wl_list_for_each(conn, &drm->outputs, link) {
			if (conn->state != WLR_DRM_CONN_CLEANUP || !conn->pageflip_pending) {
				to_close &= ~(UINT64_C(1) << i);
			}
			i++;
		}
	}

	if (to_close) {
		wlr_log(WLR_ERROR, "Timed out stopping output renderers");
	}

	wl_list_for_each(conn, &drm->outputs, link) {
		drmModeCrtc *crtc = conn->old_crtc;
		if (!crtc) {
			continue;
		}

		drmModeSetCrtc(drm->fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y,
			&conn->id, 1, &crtc->mode);
		drmModeSetCursor(drm->fd, crtc->crtc_id, 0, 0, 0);
	}
}

static void drm_connector_cleanup(struct wlr_drm_connector *conn) {
	if (!conn) {
		return;
	}

	switch (conn->state) {
	case WLR_DRM_CONN_CONNECTED:
	case WLR_DRM_CONN_CLEANUP:
		conn->output.current_mode = NULL;
		conn->desired_mode = NULL;
		struct wlr_drm_mode *mode, *tmp;
		wl_list_for_each_safe(mode, tmp, &conn->output.modes, wlr_mode.link) {
			wl_list_remove(&mode->wlr_mode.link);
			free(mode);
		}

		conn->output.enabled = false;
		conn->output.width = conn->output.height = conn->output.refresh = 0;

		memset(&conn->output.make, 0, sizeof(conn->output.make));
		memset(&conn->output.model, 0, sizeof(conn->output.model));
		memset(&conn->output.serial, 0, sizeof(conn->output.serial));

		if (conn->output.idle_frame != NULL) {
			wl_event_source_remove(conn->output.idle_frame);
			conn->output.idle_frame = NULL;
		}
		conn->output.needs_frame = false;
		conn->output.frame_pending = false;

		/* Fallthrough */
	case WLR_DRM_CONN_NEEDS_MODESET:
		wlr_log(WLR_INFO, "Emitting destruction signal for '%s'",
			conn->output.name);
		dealloc_crtc(conn);
		conn->possible_crtc = 0;
		conn->desired_mode = NULL;
		conn->pageflip_pending = false;
		wlr_signal_emit_safe(&conn->output.events.destroy, &conn->output);
		break;
	case WLR_DRM_CONN_DISCONNECTED:
		break;
	}

	conn->state = WLR_DRM_CONN_DISCONNECTED;
}
