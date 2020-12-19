#define _XOPEN_SOURCE 700
#include <assert.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <errno.h>
#include <gbm.h>
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
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/cvt.h"
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"
#include "render/pixel_format.h"
#include "render/drm_format_set.h"
#include "render/swapchain.h"
#include "render/wlr_renderer.h"
#include "util/signal.h"

static const uint32_t SUPPORTED_OUTPUT_STATE =
	WLR_OUTPUT_STATE_BACKEND_OPTIONAL |
	WLR_OUTPUT_STATE_BUFFER |
	WLR_OUTPUT_STATE_MODE |
	WLR_OUTPUT_STATE_ENABLED |
	WLR_OUTPUT_STATE_GAMMA_LUT;

bool check_drm_features(struct wlr_drm_backend *drm) {
	if (drmGetCap(drm->fd, DRM_CAP_CURSOR_WIDTH, &drm->cursor_width)) {
		drm->cursor_width = 64;
	}
	if (drmGetCap(drm->fd, DRM_CAP_CURSOR_HEIGHT, &drm->cursor_height)) {
		drm->cursor_height = 64;
	}

	uint64_t cap;
	if (drmGetCap(drm->fd, DRM_CAP_PRIME, &cap) ||
			!(cap & DRM_PRIME_CAP_IMPORT)) {
		wlr_log(WLR_ERROR, "PRIME import not supported");
		return false;
	}

	if (drm->parent) {
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

	const char *no_modifiers = getenv("WLR_DRM_NO_MODIFIERS");
	if (no_modifiers != NULL && strcmp(no_modifiers, "1") == 0) {
		wlr_log(WLR_DEBUG, "WLR_DRM_NO_MODIFIERS set, disabling modifiers");
	} else {
		ret = drmGetCap(drm->fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
		drm->addfb2_modifiers = ret == 0 && cap == 1;
		wlr_log(WLR_DEBUG, "ADDFB2 modifiers %s",
			drm->addfb2_modifiers ? "supported" : "unsupported");
	}

	return true;
}

static bool add_plane(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, const drmModePlane *drm_plane,
		uint32_t type, union wlr_drm_plane_props *props) {
	assert(!(type == DRM_PLANE_TYPE_PRIMARY && crtc->primary));
	assert(!(type == DRM_PLANE_TYPE_CURSOR && crtc->cursor));

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

	if (p->props.in_formats && drm->addfb2_modifiers) {
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
	} else if (type == DRM_PLANE_TYPE_CURSOR) {
		// Force a LINEAR layout for the cursor if the driver doesn't support
		// modifiers
		for (size_t i = 0; i < p->formats.len; ++i) {
			wlr_drm_format_set_add(&p->formats, p->formats.formats[i]->format,
				DRM_FORMAT_MOD_LINEAR);
		}
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

		// We don't really care about overlay planes, as we don't support them
		// yet.
		if (type == DRM_PLANE_TYPE_OVERLAY) {
			drmModeFreePlane(plane);
			continue;
		}

		assert(drm->num_crtcs <= 32);
		struct wlr_drm_crtc *crtc = NULL;
		for (size_t j = 0; j < drm->num_crtcs ; j++) {
			uint32_t crtc_bit = 1 << j;
			if ((plane->possible_crtcs & crtc_bit) == 0) {
				continue;
			}

			struct wlr_drm_crtc *candidate = &drm->crtcs[j];
			if ((type == DRM_PLANE_TYPE_PRIMARY && !candidate->primary) ||
					(type == DRM_PLANE_TYPE_CURSOR && !candidate->cursor)) {
				crtc = candidate;
				break;
			}
		}
		if (!crtc) {
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

static void drm_plane_set_committed(struct wlr_drm_plane *plane) {
	drm_fb_move(&plane->queued_fb, &plane->pending_fb);

	if (plane->queued_fb && plane->surf.swapchain) {
		wlr_swapchain_set_buffer_submitted(plane->surf.swapchain,
			plane->queued_fb->wlr_buf);
	}
}

static bool drm_crtc_commit(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state, uint32_t flags, bool test_only) {
	// Disallow atomic-only flags
	assert((flags & ~DRM_MODE_PAGE_FLIP_FLAGS) == 0);

	struct wlr_drm_backend *drm = conn->backend;
	struct wlr_drm_crtc *crtc = conn->crtc;
	bool ok = drm->iface->crtc_commit(conn, state, flags, test_only);
	if (ok && !test_only) {
		drm_plane_set_committed(crtc->primary);
		if (crtc->cursor != NULL) {
			drm_plane_set_committed(crtc->cursor);
		}
	} else {
		drm_fb_clear(&crtc->primary->pending_fb);
		// The set_cursor() hook is a bit special: it's not really synchronized
		// to commit() or test(). Once set_cursor() returns true, the new
		// cursor is effectively committed. So don't roll it back here, or we
		// risk ending up in a state where we don't have a cursor FB but
		// wlr_drm_connector.cursor_enabled is true.
		// TODO: fix our output interface to avoid this issue.
	}
	return ok;
}

static bool drm_crtc_page_flip(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state) {
	struct wlr_drm_crtc *crtc = conn->crtc;
	assert(crtc != NULL);

	// wlr_drm_interface.crtc_commit will perform either a non-blocking
	// page-flip, either a blocking modeset. When performing a blocking modeset
	// we'll wait for all queued page-flips to complete, so we don't need this
	// safeguard.
	if (conn->pending_page_flip_crtc && !drm_connector_state_is_modeset(state)) {
		wlr_drm_conn_log(conn, WLR_ERROR, "Failed to page-flip output: "
			"a page-flip is already pending");
		return false;
	}

	assert(drm_connector_state_active(conn, state));
	assert(plane_get_next_fb(crtc->primary));
	if (!drm_crtc_commit(conn, state, DRM_MODE_PAGE_FLIP_EVENT, false)) {
		return false;
	}

	conn->pending_page_flip_crtc = crtc->id;

	// wlr_output's API guarantees that submitting a buffer will schedule a
	// frame event. However the DRM backend will also schedule a frame event
	// when performing a modeset. Set frame_pending to true so that
	// wlr_output_schedule_frame doesn't trigger a synthetic frame event.
	conn->output.frame_pending = true;
	return true;
}

static bool drm_connector_set_pending_fb(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state) {
	struct wlr_drm_backend *drm = conn->backend;

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = crtc->primary;

	assert(state->committed & WLR_OUTPUT_STATE_BUFFER);
	switch (state->buffer_type) {
	case WLR_OUTPUT_STATE_BUFFER_RENDER:
		if (!drm_plane_lock_surface(plane, drm)) {
			wlr_drm_conn_log(conn, WLR_ERROR, "drm_plane_lock_surface failed");
			return false;
		}
		break;
	case WLR_OUTPUT_STATE_BUFFER_SCANOUT:;
		if (!drm_fb_import(&plane->pending_fb, drm, state->buffer,
				&crtc->primary->formats)) {
			wlr_drm_conn_log(conn, WLR_DEBUG,
				"Failed to import buffer for scan-out");
			return false;
		}
		break;
	}

	return true;
}

static bool drm_connector_alloc_crtc(struct wlr_drm_connector *conn);

static bool drm_connector_test(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);

	if (!conn->backend->session->active) {
		return false;
	}

	uint32_t unsupported = output->pending.committed & ~SUPPORTED_OUTPUT_STATE;
	if (unsupported != 0) {
		wlr_log(WLR_DEBUG, "Unsupported output state fields: 0x%"PRIx32,
			unsupported);
		return false;
	}

	if ((output->pending.committed & WLR_OUTPUT_STATE_ENABLED) &&
			output->pending.enabled) {
		if (output->current_mode == NULL &&
				!(output->pending.committed & WLR_OUTPUT_STATE_MODE)) {
			wlr_drm_conn_log(conn, WLR_DEBUG,
				"Can't enable an output without a mode");
			return false;
		}
	}

	if (drm_connector_state_active(conn, &output->pending)) {
		if (!drm_connector_alloc_crtc(conn)) {
			wlr_drm_conn_log(conn, WLR_DEBUG,
				"No CRTC available for this connector");
			return false;
		}
	}

	if ((output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
			output->pending.buffer_type == WLR_OUTPUT_STATE_BUFFER_SCANOUT) {
		if (!drm_connector_set_pending_fb(conn, &output->pending)) {
			return false;
		}
		if (!drm_crtc_commit(conn, &output->pending, 0, true)) {
			return false;
		}
	}

	return true;
}

bool drm_connector_supports_vrr(struct wlr_drm_connector *conn) {
	struct wlr_drm_backend *drm = conn->backend;

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}

	uint64_t vrr_capable;
	if (conn->props.vrr_capable == 0 ||
			!get_drm_prop(drm->fd, conn->id, conn->props.vrr_capable,
			&vrr_capable) || !vrr_capable) {
		wlr_drm_conn_log(conn, WLR_DEBUG, "Failed to enable adaptive sync: "
			"connector doesn't support VRR");
		return false;
	}

	if (crtc->props.vrr_enabled == 0) {
		wlr_drm_conn_log(conn, WLR_DEBUG, "Failed to enable adaptive sync: "
			"CRTC %"PRIu32" doesn't support VRR", crtc->id);
		return false;
	}

	return true;
}

static bool drm_connector_set_mode(struct wlr_drm_connector *conn,
	const struct wlr_output_state *state);

bool drm_connector_commit_state(struct wlr_drm_connector *conn,
		const struct wlr_output_state *pending) {
	struct wlr_drm_backend *drm = conn->backend;
	struct wlr_output_state state = *pending;

	if (!drm->session->active) {
		return false;
	}

	if (drm_connector_state_active(conn, &state)) {
		if (!drm_connector_alloc_crtc(conn)) {
			wlr_drm_conn_log(conn, WLR_ERROR,
				"No CRTC available for this connector");
			return false;
		}
	}

	if (state.committed & WLR_OUTPUT_STATE_BUFFER) {
		if (!drm_connector_set_pending_fb(conn, &state)) {
			return false;
		}
	}

	if (state.committed & (WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED)) {
		if ((state.committed & WLR_OUTPUT_STATE_MODE) &&
				state.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM) {
			drmModeModeInfo mode = {0};
			drm_connector_state_mode(conn, &state, &mode);

			state.mode_type = WLR_OUTPUT_STATE_MODE_FIXED;
			state.mode = wlr_drm_connector_add_mode(&conn->output, &mode);
			if (state.mode == NULL) {
				return false;
			}
		}

		if (!drm_connector_set_mode(conn, &state)) {
			return false;
		}
	} else if (state.committed & WLR_OUTPUT_STATE_BUFFER) {
		if (!drm_crtc_page_flip(conn, &state)) {
			return false;
		}
	} else if (state.committed & (WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED |
			WLR_OUTPUT_STATE_GAMMA_LUT)) {
		assert(conn->crtc != NULL);
		// TODO: maybe request a page-flip event here?
		if (!drm_crtc_commit(conn, &state, 0, false)) {
			return false;
		}
	}

	return true;
}

static bool drm_connector_commit(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);

	if (!drm_connector_test(output)) {
		return false;
	}

	return drm_connector_commit_state(conn, &output->pending);
}

static void drm_connector_rollback_render(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	return drm_surface_unset_current(&conn->crtc->primary->surf);
}

size_t drm_crtc_get_gamma_lut_size(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	if (crtc->props.gamma_lut_size == 0 || drm->iface == &legacy_iface) {
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
	struct wlr_drm_backend *drm = conn->backend;
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (crtc == NULL) {
		return 0;
	}

	return drm_crtc_get_gamma_lut_size(drm, crtc);
}

struct wlr_drm_fb *plane_get_next_fb(struct wlr_drm_plane *plane) {
	if (plane->pending_fb) {
		return plane->pending_fb;
	}
	if (plane->queued_fb) {
		return plane->queued_fb;
	}
	return plane->current_fb;
}

static bool drm_connector_test_renderer(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state) {
	struct wlr_drm_backend *drm = conn->backend;

	if (drm->iface == &legacy_iface) {
		return true;
	}

	struct wlr_drm_plane *plane = conn->crtc->primary;

	struct wlr_drm_fb *prev_fb = NULL;
	drm_fb_move(&prev_fb, &plane->pending_fb);

	bool ok = false;
	if (!drm_surface_render_black_frame(&plane->surf)) {
		goto out;
	}
	if (!drm_plane_lock_surface(plane, drm)) {
		goto out;
	}

	ok = drm_crtc_commit(conn, state, 0, true);

out:
	drm_fb_move(&plane->pending_fb, &prev_fb);
	return ok;
}

static bool drm_connector_init_renderer(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state) {
	struct wlr_drm_backend *drm = conn->backend;

	if (conn->state != WLR_DRM_CONN_CONNECTED &&
			conn->state != WLR_DRM_CONN_NEEDS_MODESET) {
		return false;
	}

	assert(conn->crtc != NULL);

	wlr_drm_conn_log(conn, WLR_DEBUG, "Initializing renderer");

	drmModeModeInfo mode = {0};
	drm_connector_state_mode(conn, state, &mode);

	struct wlr_drm_plane *plane = conn->crtc->primary;
	int width = mode.hdisplay;
	int height = mode.vdisplay;

	if (drm->addfb2_modifiers) {
		// Modifiers are supported, try to use them
		if (drm_plane_init_surface(plane, drm, width, height, true) &&
				drm_connector_test_renderer(conn, state)) {
			return true;
		}

		// If page-flipping with modifiers enabled doesn't work, retry without
		// modifiers
		wlr_drm_conn_log(conn, WLR_INFO,
			"Page-flip failed with primary FB modifiers enabled, "
			"retrying without modifiers");
	}

	if (drm_plane_init_surface(plane, drm, width, height, false) &&
			drm_connector_test_renderer(conn, state)) {
		return true;
	}

	wlr_drm_conn_log(conn, WLR_ERROR, "Failed to initialize renderer: "
		"initial page-flip failed");
	return false;
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
			wlr_drm_conn_log(conn, WLR_DEBUG,
				"Output has a desired mode and a CRTC, attempting a modeset");
			struct wlr_output_state state = {
				.committed = WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED,
				.enabled = true,
				.mode_type = WLR_OUTPUT_STATE_MODE_FIXED,
				.mode = conn->desired_mode,
			};
			drm_connector_commit_state(conn, &state);
		}
	}
}

static bool drm_connector_alloc_crtc(struct wlr_drm_connector *conn) {
	if (conn->crtc != NULL) {
		return true;
	}

	bool prev_desired_enabled = conn->desired_enabled;
	conn->desired_enabled = true;
	realloc_crtcs(conn->backend);
	conn->desired_enabled = prev_desired_enabled;

	return conn->crtc != NULL;
}

static bool drm_connector_set_mode(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state) {
	struct wlr_drm_backend *drm = conn->backend;

	struct wlr_output_mode *wlr_mode = NULL;
	if (drm_connector_state_active(conn, state)) {
		if (state->committed & WLR_OUTPUT_STATE_MODE) {
			assert(state->mode_type == WLR_OUTPUT_STATE_MODE_FIXED);
			wlr_mode = state->mode;
		} else {
			wlr_mode = conn->output.current_mode;
		}
	}

	conn->desired_enabled = wlr_mode != NULL;
	conn->desired_mode = wlr_mode;

	if (wlr_mode == NULL) {
		if (conn->crtc != NULL) {
			if (!drm_crtc_commit(conn, state, 0, false)) {
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
		wlr_drm_conn_log(conn, WLR_ERROR,
			"Cannot modeset a disconnected output");
		return false;
	}

	if (!drm_connector_alloc_crtc(conn)) {
		wlr_drm_conn_log(conn, WLR_ERROR,
			"Cannot perform modeset: no CRTC for this connector");
		return false;
	}

	wlr_drm_conn_log(conn, WLR_INFO,
		"Modesetting with '%" PRId32 "x%" PRId32 "@%" PRId32 "mHz'",
		wlr_mode->width, wlr_mode->height, wlr_mode->refresh);

	if (!drm_connector_init_renderer(conn, state)) {
		wlr_drm_conn_log(conn, WLR_ERROR,
			"Failed to initialize renderer for plane");
		return false;
	}

	// drm_crtc_page_flip expects a FB to be available
	struct wlr_drm_plane *plane = conn->crtc->primary;
	if (!plane_get_next_fb(plane)) {
		if (!drm_surface_render_black_frame(&plane->surf)) {
			return false;
		}
		if (!drm_plane_lock_surface(plane, drm)) {
			return false;
		}
	}

	if (!drm_crtc_page_flip(conn, state)) {
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

	wlr_drm_conn_log(conn, WLR_INFO, "Registered custom mode "
			"%"PRId32"x%"PRId32"@%"PRId32,
			mode->wlr_mode.width, mode->wlr_mode.height,
			mode->wlr_mode.refresh);
	wl_list_insert(&conn->output.modes, &mode->wlr_mode.link);

	return &mode->wlr_mode;
}

static bool drm_connector_set_cursor(struct wlr_output *output,
		struct wlr_buffer *buffer, int hotspot_x, int hotspot_y) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = conn->backend;
	struct wlr_drm_crtc *crtc = conn->crtc;

	if (!crtc) {
		return false;
	}

	struct wlr_drm_plane *plane = crtc->cursor;
	if (plane == NULL) {
		return false;
	}

	if (conn->cursor_hotspot_x != hotspot_x ||
			conn->cursor_hotspot_y != hotspot_y) {
		// Update cursor hotspot
		conn->cursor_x -= hotspot_x - conn->cursor_hotspot_x;
		conn->cursor_y -= hotspot_y - conn->cursor_hotspot_y;
		conn->cursor_hotspot_x = hotspot_x;
		conn->cursor_hotspot_y = hotspot_y;

		wlr_output_update_needs_frame(output);
	}

	conn->cursor_enabled = false;
	if (buffer != NULL) {
		if ((uint64_t)buffer->width != drm->cursor_width ||
				(uint64_t)buffer->height != drm->cursor_height) {
			wlr_drm_conn_log(conn, WLR_DEBUG, "Cursor buffer size mismatch");
			return false;
		}

		struct wlr_buffer *local_buf;
		if (drm->parent) {
			struct wlr_drm_format *format =
				drm_plane_pick_render_format(plane, &drm->renderer);
			if (format == NULL) {
				wlr_log(WLR_ERROR, "Failed to pick cursor plane format");
				return false;
			}

			bool ok = init_drm_surface(&plane->mgpu_surf, &drm->renderer,
				buffer->width, buffer->height, format);
			free(format);
			if (!ok) {
				return false;
			}

			local_buf = drm_surface_blit(&plane->mgpu_surf, buffer);
			if (local_buf == NULL) {
				return false;
			}
		} else {
			local_buf = wlr_buffer_lock(buffer);
		}

		bool ok = drm_fb_import(&plane->pending_fb, drm, local_buf,
			&plane->formats);
		wlr_buffer_unlock(local_buf);
		if (!ok) {
			return false;
		}

		conn->cursor_enabled = true;
		conn->cursor_width = buffer->width;
		conn->cursor_height = buffer->height;
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

	box.x -= conn->cursor_hotspot_x;
	box.y -= conn->cursor_hotspot_y;

	conn->cursor_x = box.x;
	conn->cursor_y = box.y;

	wlr_output_update_needs_frame(output);
	return true;
}

bool drm_connector_is_cursor_visible(struct wlr_drm_connector *conn) {
	return conn->cursor_enabled &&
		conn->cursor_x < conn->output.width &&
		conn->cursor_y < conn->output.height &&
		conn->cursor_x + conn->cursor_width >= 0 &&
		conn->cursor_y + conn->cursor_height >= 0;
}

static void dealloc_crtc(struct wlr_drm_connector *conn);

/**
 * Destroy the compositor-facing part of a connector.
 *
 * The connector isn't destroyed when disconnected. Only the compositor-facing
 * wlr_output interface is cleaned up.
 */
static void drm_connector_destroy_output(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);

	dealloc_crtc(conn);

	conn->state = WLR_DRM_CONN_DISCONNECTED;
	conn->desired_enabled = false;
	conn->desired_mode = NULL;
	conn->possible_crtcs = 0;
	conn->pending_page_flip_crtc = 0;

	struct wlr_drm_mode *mode, *mode_tmp;
	wl_list_for_each_safe(mode, mode_tmp, &conn->output.modes, wlr_mode.link) {
		wl_list_remove(&mode->wlr_mode.link);
		free(mode);
	}

	memset(&conn->output, 0, sizeof(struct wlr_output));
}

static const struct wlr_drm_format_set *drm_connector_get_cursor_formats(
		struct wlr_output *output, uint32_t buffer_caps) {
	if (!(buffer_caps & WLR_BUFFER_CAP_DMABUF)) {
		return NULL;
	}
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	if (!conn->crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = conn->crtc->cursor;
	if (!plane) {
		return false;
	}
	if (conn->backend->parent) {
		return &conn->backend->mgpu_formats;
	}
	return &plane->formats;
}

static void drm_connector_get_cursor_size(struct wlr_output *output,
		int *width, int *height) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	*width = (int)drm->cursor_width;
	*height = (int)drm->cursor_height;
}

static const struct wlr_drm_format_set *drm_connector_get_primary_formats(
		struct wlr_output *output, uint32_t buffer_caps) {
	if (!(buffer_caps & WLR_BUFFER_CAP_DMABUF)) {
		return NULL;
	}
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	if (!conn->crtc) {
		return NULL;
	}
	if (conn->backend->parent) {
		return &conn->backend->mgpu_formats;
	}
	return &conn->crtc->primary->formats;
}

static const struct wlr_output_impl output_impl = {
	.set_cursor = drm_connector_set_cursor,
	.move_cursor = drm_connector_move_cursor,
	.destroy = drm_connector_destroy_output,
	.attach_render = drm_connector_attach_render,
	.test = drm_connector_test,
	.commit = drm_connector_commit,
	.rollback_render = drm_connector_rollback_render,
	.get_gamma_size = drm_connector_get_gamma_size,
	.get_cursor_formats = drm_connector_get_cursor_formats,
	.get_cursor_size = drm_connector_get_cursor_size,
	.get_primary_formats = drm_connector_get_primary_formats,
};

bool wlr_output_is_drm(struct wlr_output *output) {
	return output->impl == &output_impl;
}

uint32_t wlr_drm_connector_get_id(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	return conn->id;
}

bool drm_connector_state_is_modeset(const struct wlr_output_state *state) {
	return state->committed &
		(WLR_OUTPUT_STATE_ENABLED | WLR_OUTPUT_STATE_MODE);
}

bool drm_connector_state_active(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state) {
	if (state->committed & WLR_OUTPUT_STATE_ENABLED) {
		return state->enabled;
	}
	return conn->output.enabled;
}

void drm_connector_state_mode(struct wlr_drm_connector *conn,
		const struct wlr_output_state *state, drmModeModeInfo *out) {
	assert(drm_connector_state_active(conn, state));

	struct wlr_output_mode *wlr_mode = conn->output.current_mode;
	if (state->committed & WLR_OUTPUT_STATE_MODE) {
		switch (state->mode_type) {
		case WLR_OUTPUT_STATE_MODE_FIXED:
			wlr_mode = state->mode;
			break;
		case WLR_OUTPUT_STATE_MODE_CUSTOM:;
			drmModeModeInfo mode = {0};
			generate_cvt_mode(&mode, state->custom_mode.width,
				state->custom_mode.height,
				(float)state->custom_mode.refresh / 1000, false, false);
			mode.type = DRM_MODE_TYPE_USERDEF;
			memcpy(out, &mode, sizeof(drmModeModeInfo));
			return;
		}
	}

	struct wlr_drm_mode *mode = (struct wlr_drm_mode *)wlr_mode;
	memcpy(out, &mode->drm_mode, sizeof(drmModeModeInfo));
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
	struct wlr_drm_backend *drm = conn->backend;
	if (conn->crtc == NULL) {
		return;
	}

	wlr_drm_conn_log(conn, WLR_DEBUG, "De-allocating CRTC %zu",
		conn->crtc - drm->crtcs);

	struct wlr_output_state state = {
		.committed = WLR_OUTPUT_STATE_ENABLED,
		.enabled = false,
	};
	if (!drm_crtc_commit(conn, &state, 0, false)) {
		// On GPU unplug, disabling the CRTC can fail with EPERM
		wlr_drm_conn_log(conn, WLR_ERROR, "Failed to disable CRTC %"PRIu32,
			conn->crtc->id);
	}

	drm_plane_finish_surface(conn->crtc->primary);
	drm_plane_finish_surface(conn->crtc->cursor);

	conn->cursor_enabled = false;
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
			conn->name, conn->crtc ? (int)(conn->crtc - drm->crtcs) : -1,
			conn->state, conn->desired_enabled);

		if (conn->crtc) {
			previous_match[conn->crtc - drm->crtcs] = i;
		}

		// Only search CRTCs for user-enabled outputs (that are already
		// connected or in need of a modeset)
		if ((conn->state == WLR_DRM_CONN_CONNECTED ||
				conn->state == WLR_DRM_CONN_NEEDS_MODESET) &&
				conn->desired_enabled) {
			connector_constraints[i] = conn->possible_crtcs;
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
			conn->name, connector_match[i], conn->state, conn->desired_enabled);

		// We don't need to change anything.
		if (prev_enabled && connector_match[i] == conn->crtc - drm->crtcs) {
			continue;
		}

		dealloc_crtc(conn);

		if (connector_match[i] == -1) {
			if (prev_enabled) {
				wlr_drm_conn_log(conn, WLR_DEBUG, "Output has lost its CRTC");
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

		struct wlr_output_state state = {
			.committed = WLR_OUTPUT_STATE_ENABLED,
			.enabled = true,
		};
		if (!drm_connector_init_renderer(conn, &state)) {
			wlr_drm_conn_log(conn, WLR_ERROR, "Failed to initialize renderer");
			wlr_output_update_enabled(&conn->output, false);
			continue;
		}

		wlr_output_damage_whole(&conn->output);
	}
}

static uint32_t get_possible_crtcs(int fd, drmModeRes *res,
		drmModeConnector *conn) {
	uint32_t possible_crtcs = 0;

	for (int i = 0; i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc) {
			continue;
		}

		possible_crtcs |= enc->possible_crtcs;

		drmModeFreeEncoder(enc);
	}

	return possible_crtcs;
}

static void disconnect_drm_connector(struct wlr_drm_connector *conn);

void scan_drm_connectors(struct wlr_drm_backend *drm) {
	/*
	 * This GPU is not really a modesetting device.
	 * It's just being used as a renderer.
	 */
	if (drm->num_crtcs == 0) {
		return;
	}

	wlr_log(WLR_INFO, "Scanning DRM connectors on %s", drm->name);

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

			wlr_conn->backend = drm;
			wlr_conn->state = WLR_DRM_CONN_DISCONNECTED;
			wlr_conn->id = drm_conn->connector_id;

			snprintf(wlr_conn->name, sizeof(wlr_conn->name),
				"%s-%"PRIu32, conn_get_name(drm_conn->connector_type),
				drm_conn->connector_type_id);

			wl_list_insert(drm->outputs.prev, &wlr_conn->link);
			wlr_log(WLR_INFO, "Found connector '%s'", wlr_conn->name);
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
				wlr_drm_conn_log(wlr_conn, WLR_ERROR,
					"Failed to get link status prop");
				continue;
			}

			if (link_status == DRM_MODE_LINK_STATUS_BAD) {
				// We need to reload our list of modes and force a modeset
				wlr_drm_conn_log(wlr_conn, WLR_INFO, "Bad link detected");
				disconnect_drm_connector(wlr_conn);
			}
		}

		if (wlr_conn->state == WLR_DRM_CONN_DISCONNECTED &&
				drm_conn->connection == DRM_MODE_CONNECTED) {
			wlr_log(WLR_INFO, "'%s' connected", wlr_conn->name);
			wlr_log(WLR_DEBUG, "Current CRTC: %d",
				wlr_conn->crtc ? (int)wlr_conn->crtc->id : -1);

			wlr_output_init(&wlr_conn->output, &drm->backend, &output_impl,
				drm->display);

			memcpy(wlr_conn->output.name, wlr_conn->name,
				sizeof(wlr_conn->output.name));

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

			char *subconnector = NULL;
			if (wlr_conn->props.subconnector) {
				subconnector = get_drm_prop_enum(drm->fd,
					wlr_conn->id, wlr_conn->props.subconnector);
			}
			if (subconnector && strcmp(subconnector, "Native") == 0) {
				free(subconnector);
				subconnector = NULL;
			}

			struct wlr_output *output = &wlr_conn->output;
			char description[128];
			snprintf(description, sizeof(description), "%s %s %s (%s%s%s)",
				output->make, output->model, output->serial, output->name,
				subconnector ? " via " : "", subconnector ? subconnector : "");
			wlr_output_set_description(output, description);

			free(subconnector);

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

			wlr_conn->possible_crtcs = get_possible_crtcs(drm->fd, res, drm_conn);
			if (wlr_conn->possible_crtcs == 0) {
				wlr_drm_conn_log(wlr_conn, WLR_ERROR, "No CRTC possible");
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
			wlr_log(WLR_INFO, "'%s' disconnected", wlr_conn->name);
			disconnect_drm_connector(wlr_conn);
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

		wlr_log(WLR_INFO, "'%s' disappeared", conn->name);
		destroy_drm_connector(conn);
	}

	realloc_crtcs(drm);

	for (size_t i = 0; i < new_outputs_len; ++i) {
		struct wlr_drm_connector *conn = new_outputs[i];

		wlr_drm_conn_log(conn, WLR_INFO, "Requesting modeset");
		wlr_signal_emit_safe(&drm->backend.events.new_output,
			&conn->output);
	}

	attempt_enable_needs_modeset(drm);
}

static int mhz_to_nsec(int mhz) {
	return 1000000000000LL / mhz;
}

static void handle_page_flip(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, unsigned crtc_id, void *data) {
	struct wlr_drm_backend *drm = data;

	bool found = false;
	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		if (conn->pending_page_flip_crtc == crtc_id) {
			found = true;
			break;
		}
	}
	if (!found) {
		wlr_log(WLR_DEBUG, "Unexpected page-flip event for CRTC %u", crtc_id);
		return;
	}

	conn->pending_page_flip_crtc = 0;

	if (conn->state != WLR_DRM_CONN_CONNECTED || conn->crtc == NULL) {
		wlr_drm_conn_log(conn, WLR_DEBUG,
			"Ignoring page-flip event for disabled connector");
		return;
	}

	struct wlr_drm_plane *plane = conn->crtc->primary;
	if (plane->queued_fb) {
		drm_fb_move(&plane->current_fb, &plane->queued_fb);
	}
	if (conn->crtc->cursor && conn->crtc->cursor->queued_fb) {
		drm_fb_move(&conn->crtc->cursor->current_fb,
			&conn->crtc->cursor->queued_fb);
	}

	uint32_t present_flags = WLR_OUTPUT_PRESENT_VSYNC |
		WLR_OUTPUT_PRESENT_HW_CLOCK | WLR_OUTPUT_PRESENT_HW_COMPLETION;
	/* Don't report ZERO_COPY in multi-gpu situations, because we had to copy
	 * data between the GPUs, even if we were using the direct scanout
	 * interface.
	 */
	if (!drm->parent && plane->current_fb &&
			wlr_client_buffer_get(plane->current_fb->wlr_buf)) {
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
	struct wlr_drm_backend *drm = data;

	drmEventContext event = {
		.version = 3,
		.page_flip_handler2 = handle_page_flip,
	};

	if (drmHandleEvent(fd, &event) != 0) {
		wlr_log(WLR_ERROR, "drmHandleEvent failed");
		wl_display_terminate(drm->display);
	}
	return 1;
}

static void disconnect_drm_connector(struct wlr_drm_connector *conn) {
	if (conn->state == WLR_DRM_CONN_DISCONNECTED) {
		return;
	}

	// This will cleanup the compositor-facing wlr_output, but won't destroy
	// our wlr_drm_connector.
	wlr_output_destroy(&conn->output);

	assert(conn->state == WLR_DRM_CONN_DISCONNECTED);
}

void destroy_drm_connector(struct wlr_drm_connector *conn) {
	disconnect_drm_connector(conn);

	wl_list_remove(&conn->link);
	free(conn);
}
