#define _POSIX_C_SOURCE 200112L

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/mman.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_mode.h>
#include <drm_fourcc.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/format_set.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>

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

	return true;
}

static bool parse_in_formats(int fd, struct wlr_drm_plane *plane)
{
	size_t blob_len;
	struct drm_format_modifier_blob *blob = get_drm_prop_blob(fd, plane->id,
		plane->props.in_formats, &blob_len);

	if (!blob) {
		return false;
	}

	uint32_t *fmts = (uint32_t *)((char *)blob + blob->formats_offset);

	struct drm_format_modifier *mods =
		(struct drm_format_modifier *)((char *)blob + blob->modifiers_offset);

	for (uint32_t i = 0; i < blob->count_modifiers; ++i) {
		size_t index = mods[i].offset;
		uint64_t mask = mods[i].formats;

		for (; mask; index++, mask >>= 1) {
			if (!(mask & 1)) {
				continue;
			}

			wlr_format_set_add(&plane->formats, fmts[i], mods[i].modifier);
			wlr_log(WLR_DEBUG, "Plane format: %.4s %#"PRIx64,
				(char *)&fmts[i], (uint64_t)mods[i].modifier);
		}
	}

	free(blob);
		return true;
}

static bool plane_init(struct wlr_drm_backend *drm, struct wlr_drm_plane *plane,
		uint32_t id) {
	plane->id = id;

	if (!get_drm_plane_props(drm->fd, plane->id, &plane->props)) {
		wlr_log_errno(WLR_ERROR, "Failed to get plane properties");
		return false;
	}

	uint64_t type;
	if (!get_drm_prop(drm->fd, id, plane->props.type, &type)) {
		wlr_log_errno(WLR_ERROR, "Failed to get plane type");
		return false;
	}
	plane->type = type;

	/*
	 * Exit early so we don't make a bunch of needless allocations
	 * that are going to be freed immediately.
	 */
	if (plane == DRM_PLANE_TYPE_OVERLAY) {
		return true;
	}

	drmModePlane *kms_plane = drmModeGetPlane(drm->fd, plane->id);
	if (!kms_plane) {
		wlr_log_errno(WLR_ERROR, "Failed to get plane");
		return false;
	}

	plane->possible_crtcs = kms_plane->possible_crtcs;

	// Populate formats

	bool ret = false;
	if (plane->props.in_formats) {
		ret = parse_in_formats(drm->fd, plane);
	}

	if (!ret) {
		for (uint32_t j = 0; j < kms_plane->count_formats; ++j) {
			wlr_format_set_add(&plane->formats, kms_plane->formats[j],
				DRM_FORMAT_MOD_INVALID);
			wlr_log(WLR_DEBUG, "Plane format: %.4s",
				(char *)&kms_plane->formats[j]);
		}
	}

	drmModeFreePlane(kms_plane);
	return true;
}

static void plane_finish(struct wlr_drm_plane *plane) {
	wlr_format_set_release(&plane->formats);
}

static bool crtc_init(struct wlr_drm_backend *drm, struct wlr_drm_crtc *crtc,
		uint32_t id, uint32_t pipe) {
	crtc->id = id;
	crtc->pipe = pipe;

	if (!get_drm_crtc_props(drm->fd, crtc->id, &crtc->props)) {
		wlr_log_errno(WLR_ERROR, "Failed to get CRTC properties");
		return false;
	}

	// Read gamma table size

	uint64_t max_gamma_size = 0;
	if (crtc->props.gamma_lut_size) {
		if (!get_drm_prop(drm->fd, crtc->id, crtc->props.gamma_lut_size,
				&max_gamma_size)) {
			wlr_log(WLR_ERROR, "Unable to get gamma lut size");
		}
	}
	if (max_gamma_size == 0) {
		drmModeCrtc *c = drmModeGetCrtc(drm->fd, crtc->id);
		if (c) {
			max_gamma_size = c->gamma_size;
			drmModeFreeCrtc(c);
		}
	}
	crtc->max_gamma_size = max_gamma_size;

	return true;
}

static void crtc_finish(struct wlr_drm_backend *drm, struct wlr_drm_crtc *crtc) {
	if (crtc->props.gamma_lut) {
		if (crtc->gamma_blob_id) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_blob_id);
		}
	} else {
		free(crtc->gamma_table);
	}

	plane_finish(&crtc->primary);
	plane_finish(&crtc->cursor);
}

static bool dumb_create(struct wlr_drm_backend *drm) {
	/*
	 * This function currently only deals with one size of buffer
	 * for the drm->fake use case.
	 * However, this function has been written more generally.
	 * In the future, when a wlr_shm_image is added, this is the
	 * code which would implement it.
	 */

	struct drm_mode_create_dumb create = {
		.width = 64,
		.height = 64,
		.bpp = 32,
		.flags = 0,
	};

	if (drmIoctl(drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create)) {
		wlr_log_errno(WLR_ERROR, "Failed to create dumb buffer");
		return false;
	}

	struct drm_mode_map_dumb map = {
		.handle = create.handle,
	};

	if (drmIoctl(drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) {
		wlr_log_errno(WLR_ERROR, "Failed to prepare dumb buffer");
		return false;
	}

	// Clear the colour to black

	void *data = mmap(NULL, create.size, PROT_WRITE,
		MAP_SHARED, drm->fd, map.offset);
	if (data) {
		memset(data, 0, create.size);
		munmap(data, create.size);
	} else {
		wlr_log(WLR_ERROR, "Failed to mmap dumb buffer");
		// Oh well. It'll still work, but could look terrible.
	}

	struct wlr_image *img = &drm->fake;
	img->backend = &drm->backend;
	img->width = create.width;
	img->height = create.height;
	img->format = DRM_FORMAT_XRGB8888;
	img->modifier = DRM_FORMAT_MOD_LINEAR;

	wl_signal_init(&img->release);

	uint32_t handles[4] = { create.handle };
	uint32_t strides[4] = { create.pitch };
	uint32_t offsets[4] = { map.offset };

	uint32_t fb_id;
	if (drmModeAddFB2(drm->fd, img->width, img->height, img->format,
			handles, strides, offsets, &fb_id, 0)) {
		wlr_log_errno(WLR_ERROR, "Failed to add dumb buffer");
		return false;
	}

	img->backend_priv = (void *)(uintptr_t)fb_id;

	/*
	 * We don't need the handle anymore. The buffer refcount is increased
	 * by the drmModeAddFB2 call above.
	 */
	struct drm_mode_destroy_dumb destroy = {
		.handle = create.handle,
	};
	drmIoctl(drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);

	return true;
}

bool init_drm_resources(struct wlr_drm_backend *drm) {
	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(WLR_ERROR, "Failed to get DRM resources");
		return false;
	}
	drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm->fd);
	if (!plane_res) {
		wlr_log_errno(WLR_ERROR, "Failed to get plane resources");
		goto error_res;
	}

	wlr_log(WLR_INFO, "Found %d DRM CRTCs", res->count_crtcs);

	// Populate CRTCs

	drm->num_crtcs = res->count_crtcs;
	if (drm->num_crtcs == 0) {
		goto error_plane_res;
	}
	drm->crtcs = calloc(drm->num_crtcs, sizeof(drm->crtcs[0]));
	if (!drm->crtcs) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error_res;
	}
	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		crtc_init(drm, &drm->crtcs[i], res->crtcs[i], i);
	}

	drmModeFreeResources(res);

	// Populate planes

	for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
		struct wlr_drm_plane plane = { 0 };
		if (!plane_init(drm, &plane, plane_res->planes[i])) {
			continue;
		}

		int n;
		switch (plane.type) {
		case DRM_PLANE_TYPE_PRIMARY:
			/* We assume that each primary plane can only belong to
			 * a single CRTC.
			 */
			n = ffs(plane.possible_crtcs) - 1;
			drm->crtcs[n].primary = plane;
			break;
		case DRM_PLANE_TYPE_CURSOR:
			/*
			 * We assume also assume that each cursor plane will
			 * only apply to a single CRTC. This is perhaps a less
			 * safe assumtion to make, but I've yet to see any
			 * hardware where this is not true.
			 *
			 * Not every CTRC will have a cursor plane.
			 */
			n = ffs(plane.possible_crtcs) - 1;
			drm->crtcs[n].cursor = plane;
			break;
		case DRM_PLANE_TYPE_OVERLAY:
		default:
			// Overlay planes not supported yet
			break;
		}
	}

	drmModeFreePlaneResources(plane_res);

	dumb_create(drm);

	return true;

error_plane_res:
	drmModeFreePlaneResources(plane_res);
error_res:
	drmModeFreeResources(res);
	return false;
}

void finish_drm_resources(struct wlr_drm_backend *drm) {
	if (!drm) {
		return;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		crtc_finish(drm, &drm->crtcs[i]);
	}

	free(drm->crtcs);
}

static struct wlr_drm_connector *get_drm_connector_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_drm(wlr_output));
	return (struct wlr_drm_connector *)wlr_output;
}

static size_t drm_connector_get_gamma_size(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	return conn->crtc ? conn->crtc->max_gamma_size : 0;
}

static bool drm_connector_set_gamma(struct wlr_output *output, size_t size,
		const uint16_t *r, const uint16_t *g, const uint16_t *b) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc || crtc->max_gamma_size == 0) {
		return false;
	}

	/* Some drivers (e.g. AMDGPU) have issues when trying to use the gamma
	 * property, so we temporarily provide an override until it's fixed.
	 * https://bugs.freedesktop.org/show_bug.cgi?id=107459
	 *
	 * The environment variable name contains "ATOMIC", but is not
	 * actually tied to atomic modesetting. That remains for legacy reasons.
	 */
	const char *no_prop_str = getenv("WLR_DRM_NO_ATOMIC_GAMMA");
	bool no_prop = no_prop_str && strcmp(no_prop_str, "1") == 0;

	// Using legacy gamma interface
	if (no_prop || crtc->props.gamma_lut == 0) {
		if (crtc->gamma_table) {
			free(crtc->gamma_table);
			crtc->gamma_table = NULL;
			crtc->gamma_size = 0;
		}

		bool reset = false;
		if (size == 0) {
			reset = true;
			size = conn->crtc->max_gamma_size;
		}

		crtc->gamma_table = malloc(sizeof(*crtc->gamma_table) * size * 3);
		if (!crtc->gamma_table) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}
		crtc->gamma_size = size;

		if (reset) {
			for (size_t i = 0; i < size; ++i) {
				uint16_t val = (uint32_t)0xffff * i / (size - 1);
				crtc->gamma_table[size * 0 + i] = val;
				crtc->gamma_table[size * 1 + i] = val;
				crtc->gamma_table[size * 2 + i] = val;
			}
		} else {
			memcpy(&crtc->gamma_table[size * 0], r, sizeof(*r) * size);
			memcpy(&crtc->gamma_table[size * 1], g, sizeof(*g) * size);
			memcpy(&crtc->gamma_table[size * 2], b, sizeof(*b) * size);
		}

		return true;

	// Using the GAMMA_LUT property
	} else {
		if (crtc->gamma_blob_id) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_blob_id);
			crtc->gamma_blob_id = 0;
		}

		if (size == 0) {
			return true;
		}

		struct drm_color_lut *lut = calloc(size, sizeof(*lut));
		if (!lut) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}

		for (size_t i = 0; i < size; ++i) {
			lut[i].red = r[i];
			lut[i].green = g[i];
			lut[i].blue = b[i];
		}

		int ret = drmModeCreatePropertyBlob(drm->fd, lut,
				size * sizeof(*lut), &crtc->gamma_blob_id);
		if (ret) {
			wlr_log_errno(WLR_ERROR, "Unable to create property blob");
		}

		free(lut);
		return ret == 0;
	}
}

static void drm_connector_start_renderer(struct wlr_drm_connector *conn) {
	if (conn->state != WLR_DRM_CONN_CONNECTED) {
		return;
	}
#if 0

	wlr_log(WLR_DEBUG, "Starting renderer on output '%s'", conn->output.name);

	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);
	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return;
	}

	struct wlr_drm_plane *plane = crtc->primary;
	struct wlr_image *img = plane->img;
	if (!img) {
		wlr_output_send_frame(&conn->output);
		img = plane->img;
	}

	struct wlr_drm_mode *mode = (struct wlr_drm_mode *)conn->output.current_mode;
	if (drm->iface->crtc_pageflip(drm, conn, crtc, img, &mode->drm_mode)) {
		conn->pageflip_pending = true;
		wlr_output_update_enabled(&conn->output, true);
	} else {
		wl_event_source_timer_update(conn->retry_pageflip,
			1000000.0f / conn->output.current_mode->refresh);
	}
#endif
}

#if 0
static void realloc_crtcs(struct wlr_drm_backend *drm, bool *changed_outputs);

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
			drm_connector_set_mode(&conn->output, conn->desired_mode);
		}
	}
}
#endif

bool enable_drm_connector(struct wlr_output *output, bool enable) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	//struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	if (conn->state != WLR_DRM_CONN_CONNECTED
			&& conn->state != WLR_DRM_CONN_NEEDS_MODESET) {
		return false;
	}

	conn->desire_enabled = enable;
#if 0
	if (enable && conn->crtc == NULL) {
		// Maybe we can steal a CRTC from a disabled output
		realloc_crtcs(drm, NULL);
	}

	bool ok = drm->iface->conn_enable(drm, conn, enable);
	if (!ok) {
		return false;
	}

	if (enable) {
		drm_connector_start_renderer(conn);
	} else {
		realloc_crtcs(drm, NULL);

		attempt_enable_needs_modeset(drm);
	}

	wlr_output_update_enabled(&conn->output, enable);
#endif
	return true;
}

#if 0
static ssize_t connector_index_from_crtc(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	size_t i = 0;
	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		if (conn->crtc == crtc) {
			return i;
		}
		++i;
	}
	return -1;
}

static void plane_release_img(struct wlr_drm_plane *plane) {
	if (!plane->img) {
		return;
	}

	wlr_signal_emit_safe(&plane->img->release, plane->img);
	plane->img = NULL;
}

static void realloc_planes(struct wlr_drm_backend *drm, const uint32_t *crtc_in,
		bool *changed_outputs) {
	wlr_log(WLR_DEBUG, "Reallocating planes");

	// overlay, primary, cursor
	for (size_t type = 0; type < 3; ++type) {
		if (drm->num_type_planes[type] == 0) {
			continue;
		}

		uint32_t possible[drm->num_type_planes[type] + 1];
		uint32_t crtc[drm->num_crtcs + 1];
		uint32_t crtc_res[drm->num_crtcs + 1];

		for (size_t i = 0; i < drm->num_type_planes[type]; ++i) {
			possible[i] = drm->type_planes[type][i].possible_crtcs;
		}

		for (size_t i = 0; i < drm->num_crtcs; ++i) {
			if (crtc_in[i] == UNMATCHED) {
				crtc[i] = SKIP;
			} else if (drm->crtcs[i].planes[type]) {
				crtc[i] = drm->crtcs[i].planes[type]
					- drm->type_planes[type];
			} else {
				crtc[i] = UNMATCHED;
			}
		}

		match_obj(drm->num_type_planes[type], possible,
			drm->num_crtcs, crtc, crtc_res);

		for (size_t i = 0; i < drm->num_crtcs; ++i) {
			if (crtc_res[i] == UNMATCHED || crtc_res[i] == SKIP) {
				continue;
			}

			struct wlr_drm_crtc *c = &drm->crtcs[i];
			struct wlr_drm_plane **old = &c->planes[type];
			struct wlr_drm_plane *new = &drm->type_planes[type][crtc_res[i]];

			if (*old != new) {
				wlr_log(WLR_DEBUG,
					"Assigning plane %d -> %d (type %zu) to CRTC %d",
					*old ? (int)(*old)->id : -1,
					new ? (int)new->id : -1,
					type,
					c->id);

				ssize_t conn_idx = connector_index_from_crtc(drm, c);
				if (conn_idx >= 0) {
					changed_outputs[conn_idx] = true;
				}
				if (*old) {
					plane_release_img(*old);
				}
				plane_release_img(new);
				*old = new;
			}
		}
	}
}
#endif

static void drm_connector_cleanup(struct wlr_drm_connector *conn);

bool drm_connector_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode_base) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	struct wlr_drm_mode *mode = wl_container_of(mode_base, mode, wlr_mode);

	// This has been called while inside scan_drm_connectors
	if (drm->delay_modeset) {
		conn->desire_enabled = true;
		conn->desired_mode = mode;

		return true;
	}

#if 0
	if (conn->crtc == NULL) {
		// Maybe we can steal a CRTC from a disabled output
		realloc_crtcs(drm, NULL);
	}
	if (conn->crtc == NULL) {
		wlr_log(WLR_ERROR, "Cannot modeset '%s': no CRTC for this connector",
			conn->output.name);
		// Save the desired mode for later, when we'll get a proper CRTC
		conn->desired_mode = mode;
		return false;
	}

	wlr_log(WLR_INFO, "Modesetting '%s' with '%ux%u@%u mHz'",
		conn->output.name, mode->width, mode->height, mode->refresh);

	conn->state = WLR_DRM_CONN_CONNECTED;
	conn->desired_mode = NULL;
	wlr_output_update_mode(&conn->output, mode);
	wlr_output_update_enabled(&conn->output, true);
	conn->desired_enabled = true;

	drm_connector_start_renderer(conn);

	// When switching VTs, the mode is not updated but the buffers become
	// invalid, so we need to manually damage the output here
	wlr_output_damage_whole(&conn->output);
#endif

	return true;
}

bool wlr_drm_connector_add_mode(struct wlr_output *output,
		const drmModeModeInfo *modeinfo) {
#if 0
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);

	if (modeinfo->type != DRM_MODE_TYPE_USERDEF) {
		return false;
	}

	struct wlr_output_mode *wlr_mode;
	wl_list_for_each(wlr_mode, &conn->output.modes, link) {
		struct wlr_drm_mode *mode = (struct wlr_drm_mode *)wlr_mode;
		if (memcmp(&mode->drm_mode, modeinfo, sizeof(*modeinfo)) == 0) {
			return true;
		}
	}

	struct wlr_drm_mode *mode = calloc(1, sizeof(*mode));
	if (!mode) {
		return false;
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
#endif
	return true;
}

static void drm_connector_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->transform = transform;
}

static bool drm_connector_set_cursor(struct wlr_output *output,
		struct wlr_texture *texture, int32_t scale,
		enum wl_output_transform transform,
		int32_t hotspot_x, int32_t hotspot_y, bool update_texture) {
#if 0
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}

	struct wlr_drm_plane *plane = crtc->cursor;
	if (!plane) {
		// We don't have a real cursor plane, so we make a fake one
		plane = calloc(1, sizeof(*plane));
		if (!plane) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}
		crtc->cursor = plane;
	}

	if (!plane->surf.gbm) {
		int ret;
		uint64_t w, h;
		ret = drmGetCap(drm->fd, DRM_CAP_CURSOR_WIDTH, &w);
		w = ret ? 64 : w;
		ret = drmGetCap(drm->fd, DRM_CAP_CURSOR_HEIGHT, &h);
		h = ret ? 64 : h;


		if (!drm->parent) {
			if (!init_drm_surface(&plane->surf, &drm->renderer, w, h,
					drm->renderer.gbm_format, GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT)) {
				wlr_log(WLR_ERROR, "Cannot allocate cursor resources");
				return false;
			}
		} else {
			if (!init_drm_surface(&plane->surf, &drm->parent->renderer, w, h,
					drm->parent->renderer.gbm_format, GBM_BO_USE_LINEAR)) {
				wlr_log(WLR_ERROR, "Cannot allocate cursor resources");
				return false;
			}

			if (!init_drm_surface(&plane->mgpu_surf, &drm->renderer, w, h,
					drm->renderer.gbm_format, GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT)) {
				wlr_log(WLR_ERROR, "Cannot allocate cursor resources");
				return false;
			}
		}
	}

	wlr_matrix_projection(plane->matrix, plane->surf.width,
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

		if (!drm->iface->crtc_move_cursor(drm, conn->crtc, conn->cursor_x,
				conn->cursor_y)) {
			return false;
		}

		wlr_output_update_needs_swap(output);
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

		make_drm_surface_current(&plane->surf, NULL);

		struct wlr_renderer *rend = plane->surf.renderer->wlr_rend;

		struct wlr_box cursor_box = { .width = width, .height = height };

		float matrix[9];
		wlr_matrix_project_box(matrix, &cursor_box, transform, 0, plane->matrix);

		wlr_renderer_begin(rend, plane->surf.width, plane->surf.height);
		wlr_renderer_clear(rend, (float[]){ 0.0, 0.0, 0.0, 0.0 });
		wlr_render_texture_with_matrix(rend, texture, matrix, 1.0);
		wlr_renderer_end(rend);

		swap_drm_surface_buffers(&plane->surf, NULL);

		plane->cursor_enabled = true;
	}

	if (!drm->session->active) {
		return true; // will be committed when session is resumed
	}

	struct gbm_bo *bo = plane->cursor_enabled ? plane->surf.back : NULL;
	if (bo && drm->parent) {
		bo = copy_drm_surface_mgpu(&plane->mgpu_surf, bo);
	}

	if (bo) {
		// workaround for nouveau
		// Buffers created with GBM_BO_USER_LINEAR are placed in NOUVEAU_GEM_DOMAIN_GART.
		// When the bo is attached to the cursor plane it is moved to NOUVEAU_GEM_DOMAIN_VRAM.
		// However, this does not wait for the render operations to complete, leaving an empty surface.
		// see https://bugs.freedesktop.org/show_bug.cgi?id=109631
		// The render operations can be waited for using:
		glFinish();
	}
	bool ok = drm->iface->crtc_set_cursor(drm, crtc, bo);
	if (ok) {
		wlr_output_update_needs_swap(output);
	}
	return ok;
#endif
	return true;
}

static bool drm_connector_move_cursor(struct wlr_output *output,
		int x, int y) {
#if 0
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	if (!conn->crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = conn->crtc->cursor;

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

	if (!drm->session->active) {
		return true; // will be committed when session is resumed
	}

	bool ok = drm->iface->crtc_move_cursor(drm, conn->crtc, box.x, box.y);
	if (ok) {
		wlr_output_update_needs_swap(output);
	}
	return ok;
#endif
	return true;
}

static bool drm_connector_schedule_frame(struct wlr_output *output) {
#if 0
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(output->backend);
	if (!drm->session->active) {
		return false;
	}

	// We need to figure out where we are in the vblank cycle
	// TODO: try using drmWaitVBlank and fallback to pageflipping

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = crtc->primary;
	struct wlr_image *img = plane->img;
	if (!img) {
		// We haven't swapped buffers yet -- can't do a pageflip
		wlr_output_send_frame(output);
		return true;
	}
	if (drm->parent) {
		bo = copy_drm_surface_mgpu(&plane->mgpu_surf, bo);
	}

	if (conn->pageflip_pending) {
		wlr_log(WLR_ERROR, "Skipping pageflip on output '%s'",
			conn->output.name);
		return true;
	}

	if (!drm->iface->crtc_pageflip(drm, conn, crtc, img, NULL)) {
		return false;
	}

	conn->pageflip_pending = true;
	wlr_output_update_enabled(output, true);
#endif
	return true;
}

static const struct wlr_format_set *drm_connector_get_formats(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	if (!conn->crtc) {
		return NULL;
	}

	return &conn->crtc->primary.formats;
}

static bool drm_connector_present(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	struct wlr_image *img = output->image;

	if (!img) {
		return false;
	}

	/*
	 * We can't make use of the image immediately. It'll get used during
	 * the next pageflip/modeset.
	 *
	 * If we already have a pending image we haven't made use of yet, we
	 * release it now without ever using it.
	 *
	 * TODO: presentation-time
	 */

	if (conn->desired_img && conn->desired_img != img) {
		wlr_signal_emit_safe(&conn->desired_img->release, conn->desired_img);
	}

	conn->desired_img = img;
	return true;
}

static void drm_connector_destroy(struct wlr_output *output) {
	struct wlr_drm_connector *conn = get_drm_connector_from_output(output);
	drm_connector_cleanup(conn);
	drmModeFreeCrtc(conn->old_crtc);
	wl_event_source_remove(conn->retry_pageflip);
	wl_list_remove(&conn->link);
	free(conn);
}

static const struct wlr_output_impl output_impl = {
	.enable = enable_drm_connector,
	.set_mode = drm_connector_set_mode,
	.transform = drm_connector_transform,
	.set_cursor = drm_connector_set_cursor,
	.move_cursor = drm_connector_move_cursor,
	.destroy = drm_connector_destroy,
	.set_gamma = drm_connector_set_gamma,
	.get_gamma_size = drm_connector_get_gamma_size,
	.schedule_frame = drm_connector_schedule_frame,
	.get_formats = drm_connector_get_formats,
	.present = drm_connector_present,
};

bool wlr_output_is_drm(struct wlr_output *output) {
	return output->impl == &output_impl;
}

static int retry_pageflip(void *data) {
	struct wlr_drm_connector *conn = data;
	wlr_log(WLR_INFO, "%s: Retrying pageflip", conn->output.name);
	drm_connector_start_renderer(conn);
	return 0;
}

static void dealloc_crtc(struct wlr_drm_connector *conn) {
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);
	if (conn->crtc == NULL) {
		return;
	}

	wlr_log(WLR_DEBUG, "De-allocating CRTC %zu for output '%s'",
		conn->crtc - drm->crtcs, conn->output.name);

	drm_connector_set_gamma(&conn->output, 0, NULL, NULL, NULL);

	//drm->iface->conn_enable(drm, conn, false);

	conn->crtc = NULL;
}

#if 0
static void realloc_crtcs(struct wlr_drm_backend *drm, bool *changed_outputs) {
	size_t num_outputs = wl_list_length(&drm->outputs);

	if (changed_outputs == NULL) {
		changed_outputs = calloc(num_outputs, sizeof(bool));
		if (changed_outputs == NULL) {
			wlr_log(WLR_ERROR, "Allocation failed");
			return;
		}
	}

	wlr_log(WLR_DEBUG, "Reallocating CRTCs");

	uint32_t crtc[drm->num_crtcs + 1];
	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		crtc[i] = UNMATCHED;
	}

	struct wlr_drm_connector *connectors[num_outputs + 1];

	uint32_t possible_crtc[num_outputs + 1];
	memset(possible_crtc, 0, sizeof(possible_crtc));

	wlr_log(WLR_DEBUG, "State before reallocation:");
	ssize_t i = -1;
	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		i++;
		connectors[i] = conn;

		wlr_log(WLR_DEBUG, "  '%s' crtc=%d state=%d desired_enabled=%d",
			conn->output.name,
			conn->crtc ? (int)(conn->crtc - drm->crtcs) : -1,
			conn->state, conn->desired_enabled);

		if (conn->crtc) {
			crtc[conn->crtc - drm->crtcs] = i;
		}

		// Only search CRTCs for user-enabled outputs (that are already
		// connected or in need of a modeset)
		if ((conn->state == WLR_DRM_CONN_CONNECTED ||
				conn->state == WLR_DRM_CONN_NEEDS_MODESET) &&
				conn->desired_enabled) {
			possible_crtc[i] = conn->possible_crtc;
		}
	}

	uint32_t crtc_res[drm->num_crtcs + 1];
	match_obj(wl_list_length(&drm->outputs), possible_crtc,
		drm->num_crtcs, crtc, crtc_res);

	bool matched[num_outputs + 1];
	memset(matched, false, sizeof(matched));
	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (crtc_res[i] != UNMATCHED) {
			matched[crtc_res[i]] = true;
		}
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		// We don't want any of the current monitors to be deactivated
		if (crtc[i] != UNMATCHED && !matched[crtc[i]] &&
				connectors[crtc[i]]->desired_enabled) {
			wlr_log(WLR_DEBUG, "Could not match a CRTC for connected output %d",
				crtc[i]);
			return;
		}
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (crtc_res[i] == crtc[i]) {
			continue;
		}

		// De-allocate this CRTC on previous output
		if (crtc[i] != UNMATCHED) {
			changed_outputs[crtc[i]] = true;
			dealloc_crtc(connectors[crtc[i]]);
		}

		// Assign this CRTC to next output
		if (crtc_res[i] != UNMATCHED) {
			changed_outputs[crtc_res[i]] = true;

			struct wlr_drm_connector *conn = connectors[crtc_res[i]];
			dealloc_crtc(conn);
			conn->crtc = &drm->crtcs[i];

			wlr_log(WLR_DEBUG, "Assigning CRTC %zu to output %d -> %d '%s'",
				i, crtc[i], crtc_res[i], conn->output.name);
		}
	}

	wlr_log(WLR_DEBUG, "State after reallocation:");
	wl_list_for_each(conn, &drm->outputs, link) {
		wlr_log(WLR_DEBUG, "  '%s' crtc=%d state=%d desired_enabled=%d",
			conn->output.name,
			conn->crtc ? (int)(conn->crtc - drm->crtcs) : -1,
			conn->state, conn->desired_enabled);
	}

	realloc_planes(drm, crtc_res, changed_outputs);

	// We need to reinitialize any plane that has changed
	i = -1;
	wl_list_for_each(conn, &drm->outputs, link) {
		i++;
		//struct wlr_output_mode *mode = conn->output.current_mode;

		if (conn->state != WLR_DRM_CONN_CONNECTED || !changed_outputs[i]) {
			continue;
		}

		if (conn->crtc == NULL) {
			wlr_log(WLR_DEBUG, "Output has %s lost its CRTC",
				conn->output.name);
			conn->state = WLR_DRM_CONN_NEEDS_MODESET;
			wlr_output_update_enabled(&conn->output, false);
			conn->desired_mode = conn->output.current_mode;
			wlr_output_update_mode(&conn->output, NULL);
			continue;
		}

		drm_connector_start_renderer(conn);

		wlr_output_damage_whole(&conn->output);
	}
}
#endif

/*
 * This is a wrapper over drmModeAtomicAddProperty so we get a more useful
 * return value.
 */
static int add_prop(drmModeAtomicReq *req, uint32_t id,
		uint32_t prop, uint64_t val) {
	if (!prop) {
		return 0;
	}

	return drmModeAtomicAddProperty(req, id, prop, val) >= 0;
}

static bool atomic_pick_crtc(struct wlr_drm_backend *drm,
		drmModeAtomicReq *req, struct wl_list *conn_head,
		struct wl_list *conn_link, struct wl_list *crtc_head) {
	/*
	 * Base case: We've reached the end of the list.
	 * Test the configuration to see if it's good.
	 */
	if (conn_link == conn_head) {
		return drmModeAtomicCommit(drm->fd, req,
			DRM_MODE_ATOMIC_TEST_ONLY, NULL) == 0;
	}

	/*
	 * We loop through all of the available CRTCs and try each one until we
	 * get a configuration that works.
	 */

	struct wlr_drm_connector *conn = wl_container_of(conn_link, conn, state_link);
	struct wlr_drm_mode *mode = conn->desired_mode;
	struct wlr_image *img = conn->desired_img ? conn->desired_img : &drm->fake;

	if (!mode->blob_id && drmModeCreatePropertyBlob(drm->fd, &mode->drm_mode,
			sizeof(mode->drm_mode), &mode->blob_id)) {
		wlr_log_errno(WLR_ERROR, "Unable to create property blob");
		return false;
	}

	struct wlr_drm_crtc *crtc;
	wl_list_for_each(crtc, crtc_head, state_link) {
		// Not compatible
		if ((conn->possible_crtcs & (1 << crtc->pipe)) == 0) {
			continue;
		}

		int cursor = drmModeAtomicGetCursor(req);
		struct wl_list *prev = crtc->state_link.prev;
		struct wlr_drm_plane *plane = &crtc->primary;

		/*
		 * We remove the CRTC from the list so that subsequent
		 * recursive calls can't take it either.
		 * The list must be restored to its original state before
		 * the loop continues.
		 */
		wl_list_remove(&crtc->state_link);

		int ret = 1;

		if (crtc->props.gamma_lut) {
			ret &= add_prop(req, crtc->id, crtc->props.gamma_lut,
				crtc->gamma_blob_id);
		}
		ret &= add_prop(req, conn->id, conn->props.crtc_id, crtc->id);
		ret &= add_prop(req, conn->id, conn->props.link_status,
			DRM_MODE_LINK_STATUS_GOOD);
		ret &= add_prop(req, crtc->id, crtc->props.active, 1);
		ret &= add_prop(req, crtc->id, crtc->props.mode_id, mode->blob_id);
		// SRC_* properties are in 16.16 fixed point
		ret &= add_prop(req, plane->id, plane->props.src_x, 0);
		ret &= add_prop(req, plane->id, plane->props.src_y, 0);
		ret &= add_prop(req, plane->id, plane->props.src_w,
			(uint64_t)img->width << 16);
		ret &= add_prop(req, plane->id, plane->props.src_h,
			(uint64_t)img->height << 16);
		ret &= add_prop(req, plane->id, plane->props.crtc_x, 0);
		ret &= add_prop(req, plane->id, plane->props.crtc_y, 0);
		ret &= add_prop(req, plane->id, plane->props.crtc_w,
			mode->wlr_mode.width);
		ret &= add_prop(req, plane->id, plane->props.crtc_h,
			mode->wlr_mode.height);
		ret &= add_prop(req, plane->id, plane->props.fb_id,
			(uintptr_t)img->backend_priv);
		ret &= add_prop(req, plane->id, plane->props.crtc_id, crtc->id);

		if (!ret) {
			wlr_log(WLR_ERROR, "Failed to set properties");
			goto error;
		}

		if (atomic_pick_crtc(drm, req, conn_head,
				conn->state_link.next, crtc_head)) {
			conn->crtc = crtc;
			conn->locked_img = img;
			conn->desired_img = NULL;
			conn->pageflip_pending = true;
			return true;
		}

error:
		drmModeAtomicSetCursor(req, cursor);
		wl_list_insert(prev, &crtc->state_link);
	}

	return false;
}

static void atomic_apply_state(struct wlr_drm_backend *drm) {
	drmModeAtomicReq *req = drmModeAtomicAlloc();

	struct wl_list enabled, disabled, crtcs;
	wl_list_init(&enabled);
	wl_list_init(&disabled);
	wl_list_init(&crtcs);

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		drm->crtcs[i].in_list = false;
	}

	/*
	 * We store a connector and its previously associated CRTC
	 * in the same position in their respective lists, so that they
	 * are significantly more likely to keep their association.
	 *
	 * e.g. enabled[0]->crtc == crtcs[0]
	 *
	 * All connectors and CRTCs without an existing association
	 * are stored at the end of their lists.
	 */

	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		if (conn->desire_enabled) {
			if (conn->crtc) {
				wl_list_insert(&enabled, &conn->state_link);
				wl_list_insert(&crtcs, &conn->crtc->state_link);
				conn->crtc->in_list = true;
			} else {
				// Add unassocited connector to the end
				wl_list_insert(enabled.prev, &conn->state_link);
			}
		} else {
			wl_list_insert(&disabled, &conn->state_link);
		}
	}

	// Add unassociated CRTCs to the end

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (!drm->crtcs[i].in_list) {
			wl_list_insert(crtcs.prev, &drm->crtcs[i].state_link);
		}
	}

	// Find the configuration

	while (!wl_list_empty(&enabled)) {
		if (atomic_pick_crtc(drm, req, &enabled, enabled.next, &crtcs)) {
			break;
		}

		// Keep popping elements off of the end until it works

		struct wlr_drm_connector *conn =
			wl_container_of(enabled.prev, conn, state_link);
		wl_list_remove(&conn->state_link);
		wl_list_insert(&disabled, &conn->state_link);
	}

	if (drmModeAtomicCommit(drm->fd, req,
			DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET, drm)) {
		wlr_log_errno(WLR_ERROR, "Atomic modeset failed");

		/*
		 * This should ideally never happen, because we've run this
		 * past TEST_ONLY, but it can happen in some situations, which
		 * probably indicates a bug in our code.
		 *
		 * Instead of desperately scrambling to try again to find a
		 * configuration that works (which we probably won't), the only
		 * sensible thing to do here it to shut down. The backend is
		 * basically completely stalled.
		 */
		wl_display_destroy_clients(drm->display);
		wl_display_destroy(drm->display);
		goto error;
	}

	wlr_log(WLR_DEBUG, "CRTC mapping:");

	wl_list_for_each(conn, &enabled, state_link) {
		wlr_log(WLR_DEBUG, "  CRTC %"PRIu32" <-> Connector '%s'",
			conn->crtc->pipe, conn->output.name);

		wlr_output_update_enabled(&conn->output, true);
		wlr_output_update_mode(&conn->output, &conn->desired_mode->wlr_mode);
	}

	wl_list_for_each(conn, &disabled, state_link) {
		wlr_output_update_enabled(&conn->output, false);
		wlr_output_update_mode(&conn->output, NULL);
	}

	/*
	 * Unset all of the CRTCs we're not using.
	 * We can't do this in the commit above, because we can't ask for an
	 * event on a disabled CRTC.
	 */

	drmModeAtomicSetCursor(req, 0);
	struct wlr_drm_crtc *crtc;
	wl_list_for_each(crtc, &crtcs, state_link) {
		wlr_log(WLR_DEBUG, "  CRTC %"PRIu32" unused", crtc->pipe);
		add_prop(req, crtc->id, crtc->props.active, 0);
		add_prop(req, crtc->id, crtc->props.mode_id, 0);
	}
	drmModeAtomicCommit(drm->fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);

error:
	drmModeAtomicFree(req);
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

	/*
	 * Sometimes DP MST connectors report no encoders, so we'll loop though
	 * all of the encoders of the MST type instead.
	 * TODO: See if there is a better solution.
	 */

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

static struct wlr_drm_connector *connector_create(struct wlr_drm_backend *drm,
		drmModeRes *res, drmModeConnector *kms_conn) {
	struct wlr_drm_connector *conn = calloc(1, sizeof(*conn));
	if (!conn) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	conn->id = kms_conn->connector_id;
	conn->state = WLR_DRM_CONN_DISCONNECTED;

	if (!get_drm_connector_props(drm->fd, conn->id, &conn->props)) {
		wlr_log_errno(WLR_ERROR, "Failed to get connector properties");
		goto error_conn;
	}

	conn->possible_crtcs =
		get_possible_crtcs(drm->fd, res, kms_conn, conn->props.path != 0);
	if (conn->possible_crtcs == 0) {
		wlr_log(WLR_ERROR, "No CRTC possible for connector '%s'",
			conn->output.name);
	}

	struct wl_event_loop *ev = wl_display_get_event_loop(drm->display);
	conn->retry_pageflip = wl_event_loop_add_timer(ev, retry_pageflip, conn);
	if (!conn->retry_pageflip) {
		wlr_log_errno(WLR_ERROR, "Failed to create timer source");
		goto error_conn;
	}

	wlr_output_init(&conn->output, &drm->backend, &output_impl, drm->display);

	snprintf(conn->output.name, sizeof(conn->output.name),
		"%s-%"PRIu32, conn_get_name(kms_conn->connector_type),
		kms_conn->connector_type_id);

	drmModeEncoder *enc = drmModeGetEncoder(drm->fd, kms_conn->encoder_id);
	if (enc) {
		conn->old_crtc = drmModeGetCrtc(drm->fd, enc->crtc_id);
	}

	wl_list_insert(drm->outputs.prev, &conn->link);

	return conn;

error_conn:
	free(conn);
	return NULL;
}

static const int32_t subpixel_map[] = {
	[DRM_MODE_SUBPIXEL_UNKNOWN] = WL_OUTPUT_SUBPIXEL_UNKNOWN,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_RGB] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_BGR] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR,
	[DRM_MODE_SUBPIXEL_VERTICAL_RGB] = WL_OUTPUT_SUBPIXEL_VERTICAL_RGB,
	[DRM_MODE_SUBPIXEL_VERTICAL_BGR] = WL_OUTPUT_SUBPIXEL_VERTICAL_BGR,
	[DRM_MODE_SUBPIXEL_NONE] = WL_OUTPUT_SUBPIXEL_NONE,
};

static void connector_populate(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, drmModeConnector *kms_conn) {
	conn->output.phys_width = kms_conn->mmWidth;
	conn->output.phys_height = kms_conn->mmHeight;
	wlr_log(WLR_INFO, "Physical size: %"PRId32"x%"PRId32,
		conn->output.phys_width, conn->output.phys_height);

	conn->output.subpixel = subpixel_map[kms_conn->subpixel];

	size_t edid_len = 0;
	uint8_t *edid =
		get_drm_prop_blob(drm->fd, conn->id, conn->props.edid, &edid_len);
	parse_edid(&conn->output, edid_len, edid);
	free(edid);

	wlr_log(WLR_INFO, "Detected modes:");

	for (int i = 0; i < kms_conn->count_modes; ++i) {
		// We don't support interlacing
		if (kms_conn->modes[i].flags & DRM_MODE_FLAG_INTERLACE) {
			continue;
		}

		struct wlr_drm_mode *mode = calloc(1, sizeof(*mode));
		if (!mode) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			continue;
		}

		mode->drm_mode = kms_conn->modes[i];
		mode->wlr_mode.width = mode->drm_mode.hdisplay;
		mode->wlr_mode.height = mode->drm_mode.vdisplay;
		mode->wlr_mode.refresh = calculate_refresh_rate(&mode->drm_mode);

		wlr_log(WLR_INFO, "  %"PRId32"x%"PRId32"@%"PRId32,
			mode->wlr_mode.width, mode->wlr_mode.height,
			mode->wlr_mode.refresh);

		wl_list_insert(&conn->output.modes, &mode->wlr_mode.link);
	}
}

static bool link_status_bad(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn) {
	if (!conn->props.link_status) {
		return false;
	}
	uint64_t link_status;
	if (!get_drm_prop(drm->fd, conn->id, conn->props.link_status,
			&link_status)) {
		// If it fails, who really knows what the link status is?
		wlr_log_errno(WLR_ERROR, "Failed to get link status for '%s'",
			conn->output.name);
		return false;
	}
	return link_status == DRM_MODE_LINK_STATUS_BAD;
}

void scan_drm_connectors(struct wlr_drm_backend *drm) {
	wlr_log(WLR_INFO, "Scanning DRM connectors");

	struct wlr_drm_connector *iter, *tmp;
	wl_list_for_each(iter, &drm->outputs, link) {
		iter->seen = false;
	}

	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(WLR_ERROR, "Failed to get DRM resources");
		return;
	}

	// Accumulate state and apply it all at the end
	drm->delay_modeset = true;

	struct wl_list new_connectors;
	wl_list_init(&new_connectors);

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *kms_conn =
			drmModeGetConnector(drm->fd, res->connectors[i]);
		if (!kms_conn) {
			wlr_log_errno(WLR_ERROR, "Failed to get DRM connector");
			continue;
		}

		// See if we already know about this connector

		struct wlr_drm_connector *conn = NULL;
		wl_list_for_each(iter, &drm->outputs, link) {
			if (iter->id == kms_conn->connector_id) {
				conn = iter;
				break;
			}
		}

		// Create it if we don't

		if (!conn) {
			conn = connector_create(drm, res, kms_conn);
			if (!conn) {
				drmModeFreeConnector(kms_conn);
				continue;
			}

			wlr_log(WLR_INFO, "Found connector '%s'", conn->output.name);
		}
		conn->seen = true;

		/*
		 * link-status is an asynchronous way for the kernel to tell us
		 * that it failed to modeset after we've already committed the state.
		 * e.g. Run out of DisplayPort bandwidth due to MST or low-quality
		 * cables.
		 *
		 * Basically all of our assumptions are now wrong, so we tear
		 * the entire connector down and bring it up again.
		 */
		if (link_status_bad(drm, conn)) {
			wlr_log(WLR_ERROR, "Link status went bad for '%s'", conn->output.name);
			drm_connector_cleanup(conn);
		}

		// The connector has been plugged in
		if (conn->state == WLR_DRM_CONN_DISCONNECTED &&
				kms_conn->connection == DRM_MODE_CONNECTED) {
			wlr_log(WLR_INFO, "'%s' connected", conn->output.name);

			connector_populate(drm, conn, kms_conn);

			wl_list_insert(&new_connectors, &conn->new_link);
			conn->state = WLR_DRM_CONN_NEEDS_MODESET;

		// The connector has been unplugged
		} else if ((conn->state == WLR_DRM_CONN_CONNECTED ||
				conn->state == WLR_DRM_CONN_NEEDS_MODESET) &&
				kms_conn->connection != DRM_MODE_CONNECTED) {
			wlr_log(WLR_INFO, "'%s' disconnected", conn->output.name);

			drm_connector_cleanup(conn);
		}

		drmModeFreeConnector(kms_conn);
	}

	drmModeFreeResources(res);

	// Clean up connectors that disappeared (e.g. DisplayPort MST)

	wl_list_for_each_safe(iter, tmp, &drm->outputs, link) {
		if (iter->seen) {
			continue;
		}

		wlr_log(WLR_INFO, "'%s' disappeared", iter->output.name);
		drm_connector_cleanup(iter);

		// Delay freeing the output until we get receive the event
		if (iter->pageflip_pending) {
			iter->state = WLR_DRM_CONN_DISAPPEARED;
		} else {
			wlr_output_destroy(&iter->output);
		}
	}

	/*
	 * Tell the user about the new outputs.
	 * While this is happening, we are collecting the requested modes
	 * instead of applying them.
	 */

	wl_list_for_each(iter, &new_connectors, new_link) {
		wlr_log(WLR_INFO, "Requesting modeset for '%s'", iter->output.name);
		wlr_signal_emit_safe(&drm->backend.events.new_output, &iter->output);
	}

#if 0
	/*
	 * We need to make sure we have a frame to show, otherwise modesetting
	 * will fail. Ask the user for one if they haven't provided it already.
	 */

	wl_list_for_each(iter, &drm->outputs, link) {
		if (!iter->desire_enabled) {
			continue;
		}
		if (iter->desired_img) {
			continue;
		}

		wlr_output_send_frame(&iter->output);

		// I guess you really don't want to be enabled
		if (!iter->desired_img) {
			iter->desire_enabled = false;
		}
	}
#endif

	// Now we try to apply all of the modes at once

	atomic_apply_state(drm);
}

#if 0
static int mhz_to_nsec(int mhz) {
	return 1000000000000LL / mhz;
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, void *data) {
	struct wlr_drm_connector *conn = data;
	struct wlr_drm_backend *drm =
		get_drm_backend_from_backend(conn->output.backend);

	conn->pageflip_pending = false;

	if (conn->state == WLR_DRM_CONN_DISAPPEARED) {
		wlr_output_destroy(&conn->output);
		return;
	}

	if (conn->state != WLR_DRM_CONN_CONNECTED || conn->crtc == NULL) {
		return;
	}

#if 0
	post_drm_surface(&conn->crtc->primary->surf);
	if (drm->parent) {
		post_drm_surface(&conn->crtc->primary->mgpu_surf);
	}
#endif

	struct timespec present_time = {
		.tv_sec = tv_sec,
		.tv_nsec = tv_usec * 1000,
	};
	struct wlr_output_event_present present_event = {
		.when = &present_time,
		.seq = seq,
		.refresh = mhz_to_nsec(conn->output.refresh),
		.flags = WLR_OUTPUT_PRESENT_VSYNC | WLR_OUTPUT_PRESENT_HW_CLOCK |
			WLR_OUTPUT_PRESENT_HW_COMPLETION,
	};
	wlr_output_send_present(&conn->output, &present_event);

	if (drm->session->active) {
		wlr_output_send_frame(&conn->output);
	}
}
#endif

static void pageflip_handler(int fd, unsigned seq, unsigned tv_sec, unsigned tv_usec,
		unsigned crtc_id, void *data) {
	struct wlr_drm_backend *drm = data;
	wlr_log(WLR_DEBUG, "pageflip on CRTC %u at %u.%u", crtc_id, tv_sec, tv_usec);

	// Find the connector for this pageflip
	struct wlr_drm_connector *conn = NULL, *iter;
	wl_list_for_each(iter, &drm->outputs, link) {
		if (iter->crtc && iter->crtc->id == crtc_id) {
			conn = iter;
			break;
		}
	}

	// We haven't been managing our state properly
	assert(conn);
	assert(conn->locked_img);
	assert(conn->pageflip_pending);
	conn->pageflip_pending = false;

	if (conn->state == WLR_DRM_CONN_DISAPPEARED) {
		wlr_output_destroy(&conn->output);
		return;
	}

	// The VT has changed, so we have to stop the rendering loop here
	if (!drm->session->active) {
		return;
	}

	wlr_output_send_frame(&conn->output);

	if (conn->desired_img) {
		wlr_signal_emit_safe(&conn->locked_img->release,
			conn->locked_img);
		conn->locked_img = conn->desired_img;
		conn->desired_img = NULL;
	}

	struct wlr_image *img = conn->locked_img;
	struct wlr_drm_crtc *crtc = conn->crtc;
	struct wlr_drm_plane *plane = &crtc->primary;
	struct wlr_drm_mode *mode = conn->desired_mode;
	drmModeAtomicReq *req = drmModeAtomicAlloc();

	int ret = 1;

	if (crtc->props.gamma_lut) {
		ret &= add_prop(req, crtc->id, crtc->props.gamma_lut,
			crtc->gamma_blob_id);
	}
	ret &= add_prop(req, conn->id, conn->props.crtc_id, crtc->id);
	ret &= add_prop(req, conn->id, conn->props.link_status,
		DRM_MODE_LINK_STATUS_GOOD);
	ret &= add_prop(req, crtc->id, crtc->props.active, 1);
	ret &= add_prop(req, crtc->id, crtc->props.mode_id, mode->blob_id);
	// SRC_* properties are in 16.16 fixed point
	ret &= add_prop(req, plane->id, plane->props.src_x, 0);
	ret &= add_prop(req, plane->id, plane->props.src_y, 0);
	ret &= add_prop(req, plane->id, plane->props.src_w,
		(uint64_t)img->width << 16);
	ret &= add_prop(req, plane->id, plane->props.src_h,
		(uint64_t)img->height << 16);
	ret &= add_prop(req, plane->id, plane->props.crtc_x, 0);
	ret &= add_prop(req, plane->id, plane->props.crtc_y, 0);
	ret &= add_prop(req, plane->id, plane->props.crtc_w,
		mode->wlr_mode.width);
	ret &= add_prop(req, plane->id, plane->props.crtc_h,
		mode->wlr_mode.height);
	ret &= add_prop(req, plane->id, plane->props.fb_id,
		(uintptr_t)img->backend_priv);
	ret &= add_prop(req, plane->id, plane->props.crtc_id, crtc->id);

	if (!ret) {
		wlr_log(WLR_ERROR, "Failed to set properties");
		goto error;
	}

	if (drmModeAtomicCommit(drm->fd, req,
			DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, drm)) {
		wlr_log(WLR_ERROR, "Atomic commit failed");
	} else {
		conn->pageflip_pending = true;
	}

error:
	drmModeAtomicFree(req);
}

int handle_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = 3,
		//.page_flip_handler = page_flip_handler,
		.page_flip_handler2 = pageflip_handler,
	};

	drmHandleEvent(fd, &event);
	return 1;
}

void restore_drm_outputs(struct wlr_drm_backend *drm) {
	uint64_t to_close = (1L << wl_list_length(&drm->outputs)) - 1;

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
				to_close &= ~(1 << i);
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
	}
}

static void drm_connector_cleanup(struct wlr_drm_connector *conn) {
	if (!conn) {
		return;
	}

	switch (conn->state) {
	case WLR_DRM_CONN_CONNECTED:
	case WLR_DRM_CONN_CLEANUP:;
		//struct wlr_drm_crtc *crtc = conn->crtc;

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
		conn->output.needs_swap = false;
		conn->output.frame_pending = false;

		/* Fallthrough */
	case WLR_DRM_CONN_NEEDS_MODESET:
		wlr_log(WLR_INFO, "Emitting destruction signal for '%s'",
			conn->output.name);
		dealloc_crtc(conn);
		conn->possible_crtcs = 0;
		conn->desired_mode = NULL;
		wlr_signal_emit_safe(&conn->output.events.destroy, &conn->output);
		break;
	case WLR_DRM_CONN_DISCONNECTED:
		break;
	case WLR_DRM_CONN_DISAPPEARED:
		return; // don't change state
	}

	conn->state = WLR_DRM_CONN_DISCONNECTED;
}
