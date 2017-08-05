#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <GLES3/gl3.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "drm.h"
#include "drm-util.h"

bool wlr_drm_check_features(struct wlr_backend_state *drm) {
	if (drmSetClientCap(drm->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		wlr_log(L_INFO, "DRM universal planes unsupported");
		return false;
	}

	if (drmSetClientCap(drm->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		wlr_log(L_INFO, "Atomic modesetting unsupported");
	}

	return true;
}

static int cmp_plane(const void *arg1, const void *arg2)
{
	const struct wlr_drm_plane *a = arg1;
	const struct wlr_drm_plane *b = arg2;

	return (int)a->type - (int)b->type;
}

static bool init_planes(struct wlr_backend_state *drm)
{
	drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm->fd);
	if (!plane_res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM plane resources");
		return false;
	}

	wlr_log(L_INFO, "Found %"PRIu32" DRM planes", plane_res->count_planes);

	if (plane_res->count_planes == 0) {
		drmModeFreePlaneResources(plane_res);
		return true;
	}

	drm->num_planes = plane_res->count_planes;
	drm->planes = calloc(drm->num_planes, sizeof(*drm->planes));
	if (!drm->planes) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < drm->num_planes; ++i) {
		struct wlr_drm_plane *p = &drm->planes[i];

		drmModePlane *plane = drmModeGetPlane(drm->fd, plane_res->planes[i]);
		if (!plane) {
			wlr_log_errno(L_ERROR, "Failed to get DRM plane");
			goto error_planes;
		}

		p->id = plane->plane_id;
		p->possible_crtcs = plane->possible_crtcs;
		uint64_t type;

		if (!wlr_drm_get_plane_props(drm->fd, p->id, &p->props) ||
				!wlr_drm_get_prop(drm->fd, p->id, p->props.type, &type)) {
			drmModeFreePlane(plane);
			goto error_planes;
		}

		p->type = type;
		drm->num_type_planes[type]++;

		drmModeFreePlane(plane);
	}

	wlr_log(L_INFO, "(%zu overlay, %zu primary, %zu cursor)",
		drm->num_overlay_planes, drm->num_primary_planes, drm->num_cursor_planes);

	qsort(drm->planes, drm->num_planes, sizeof(*drm->planes), cmp_plane);

	drm->overlay_planes = drm->planes;
	drm->primary_planes = drm->overlay_planes + drm->num_overlay_planes;
	drm->cursor_planes = drm->primary_planes + drm->num_primary_planes;

	return true;

error_planes:
	free(drm->planes);
error_res:
	drmModeFreePlaneResources(plane_res);
	return false;
}

bool wlr_drm_resources_init(struct wlr_backend_state *drm) {
	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM resources");
		return false;
	}

	wlr_log(L_INFO, "Found %d DRM CRTCs", res->count_crtcs);

	drm->num_crtcs = res->count_crtcs;
	drm->crtcs = calloc(drm->num_crtcs, sizeof(drm->crtcs[0]));
	if (!drm->crtcs) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];
		crtc->id = res->crtcs[i];
		wlr_drm_get_crtc_props(drm->fd, crtc->id, &crtc->props);
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

void wlr_drm_resources_free(struct wlr_backend_state *drm) {
	if (!drm)
		return;

	free(drm->crtcs);
	free(drm->planes);
}

bool wlr_drm_renderer_init(struct wlr_drm_renderer *renderer, int fd) {
	renderer->gbm = gbm_create_device(fd);
	if (!renderer->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM device: %s", strerror(errno));
		return false;
	}

	if (!wlr_egl_init(&renderer->egl, EGL_PLATFORM_GBM_MESA, renderer->gbm)) {
		gbm_device_destroy(renderer->gbm);
		return false;
	}

	renderer->fd = fd;
	return true;
}

void wlr_drm_renderer_free(struct wlr_drm_renderer *renderer) {
	if (!renderer)
		return;

	wlr_egl_free(&renderer->egl);
	gbm_device_destroy(renderer->gbm);
}

static void free_fb(struct gbm_bo *bo, void *data) {
	uint32_t id = (uintptr_t)data;

	if (id) {
		struct gbm_device *gbm = gbm_bo_get_device(bo);
		drmModeRmFB(gbm_device_get_fd(gbm), id);
	}
}

static uint32_t get_fb_for_bo(struct gbm_bo *bo) {
	uint32_t id = (uintptr_t)gbm_bo_get_user_data(bo);
	if (id)
		return id;

	struct gbm_device *gbm = gbm_bo_get_device(bo);
	drmModeAddFB(gbm_device_get_fd(gbm), gbm_bo_get_width(bo), gbm_bo_get_height(bo),
		24, 32, gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, &id);

	gbm_bo_set_user_data(bo, (void *)(uintptr_t)id, free_fb);

	return id;
}

static void wlr_drm_plane_make_current(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane) {
	eglMakeCurrent(renderer->egl.display, plane->egl, plane->egl,
		renderer->egl.context);
}

static void wlr_drm_plane_swap_buffers(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane) {
	if (plane->front)
		gbm_surface_release_buffer(plane->gbm, plane->front);

	eglSwapBuffers(renderer->egl.display, plane->egl);

	plane->front = plane->back;
	plane->back = gbm_surface_lock_front_buffer(plane->gbm);
}

static void wlr_drm_output_make_current(struct wlr_output_state *output) {
	wlr_drm_plane_make_current(output->renderer, output->crtc->primary);
}

static void wlr_drm_output_swap_buffers(struct wlr_output_state *output) {
	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_drm_crtc *crtc = output->crtc;
	struct wlr_drm_plane *plane = crtc->primary;

	wlr_drm_plane_swap_buffers(renderer, plane);
	uint32_t fb_id = get_fb_for_bo(plane->back);

	drmModePageFlip(renderer->fd, crtc->id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, output);
	output->pageflip_pending = true;
}

void wlr_drm_output_start_renderer(struct wlr_output_state *output) {
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_output_mode *mode = output->base->current_mode;
	struct wlr_drm_crtc *crtc = output->crtc;
	struct wlr_drm_plane *plane = crtc->primary;

	struct gbm_bo *bo = plane->front;
	if (!bo) {
		// Render a black frame to start the rendering loop
		wlr_drm_plane_make_current(renderer, plane);
		glViewport(0, 0, plane->width, plane->height);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		wlr_drm_plane_swap_buffers(renderer, plane);

		bo = plane->back;
	}

	uint32_t fb_id = get_fb_for_bo(bo);
	drmModeSetCrtc(renderer->fd, crtc->id, fb_id, 0, 0,
		&output->connector, 1, &mode->state->mode);
	drmModePageFlip(renderer->fd, crtc->id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, output);
	output->pageflip_pending = true;
}

static bool plane_init_renderer(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane, struct wlr_output_mode *mode) {
	plane->width = mode->width;
	plane->height = mode->height;

	plane->gbm = gbm_surface_create(renderer->gbm, mode->width,
		mode->height, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!plane->gbm) {
		wlr_log_errno(L_ERROR, "Failed to create GBM surface for plane");
		return false;
	}

	plane->egl = wlr_egl_create_surface(&renderer->egl, plane->gbm);
	if (plane->egl == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface for plane");
		return false;
	}

	return true;
}

static int find_id(const void *item, const void *cmp_to) {
	const struct wlr_output_state *output = item;
	const uint32_t *id = cmp_to;

	if (output->connector < *id) {
		return -1;
	} else if (output->connector > *id) {
		return 1;
	} else {
		return 0;
	}
}

static void wlr_drm_output_enable(struct wlr_output_state *output, bool enable) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	if (enable) {
		drmModeConnectorSetProperty(state->fd, output->connector, output->props.dpms,
			DRM_MODE_DPMS_ON);

		// Start rendering loop again by drawing a black frame
		wlr_drm_output_make_current(output);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		wlr_drm_output_swap_buffers(output);
	} else {
		drmModeConnectorSetProperty(state->fd, output->connector, output->props.dpms,
			DRM_MODE_DPMS_OFF);
	}
}

static void realloc_planes(struct wlr_backend_state *drm, const uint32_t *crtc_in) {
	// overlay, primary, cursor
	for (int type = 0; type < 3; ++type) {
		if (drm->num_type_planes[type] == 0)
			continue;

		uint32_t possible[drm->num_type_planes[type]];
		uint32_t crtc[drm->num_crtcs];
		uint32_t crtc_res[drm->num_crtcs];

		for (size_t i = 0; i < drm->num_type_planes[type]; ++i)
			possible[i] = drm->type_planes[type][i].possible_crtcs;

		for (size_t i = 0; i < drm->num_crtcs; ++i) {
			if (crtc_in[i] == UNMATCHED)
				crtc[i] = SKIP;
			else if (drm->crtcs[i].planes[type])
				crtc[i] = drm->crtcs[i].planes[type] - drm->type_planes[type];
			else
				crtc[i] = UNMATCHED;
		}

		match_obj(drm->num_type_planes[type], possible, drm->num_crtcs, crtc, crtc_res);

		for (size_t i = 0; i < drm->num_crtcs; ++i) {
			if (crtc_res[i] == UNMATCHED || crtc_res[i] == SKIP)
				continue;

			struct wlr_drm_crtc *c = &drm->crtcs[i];
			c->planes[type] = &drm->type_planes[type][crtc_res[i]];
		}
	}
}

static void realloc_crtcs(struct wlr_backend_state *drm, struct wlr_output_state *output) {
	bool handled[drm->outputs->length];
	uint32_t crtc[drm->num_crtcs];
	uint32_t crtc_res[drm->num_crtcs];
	uint32_t possible_crtc[drm->outputs->length];

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		crtc[i] = UNMATCHED;
	}

	memset(possible_crtc, 0, sizeof(possible_crtc));
	memset(handled, 0, sizeof(handled));

	size_t index;
	for (size_t i = 0; i < drm->outputs->length; ++i) {
		struct wlr_output_state *o = drm->outputs->items[i];
		if (o == output) {
			index = i;
		}

		if (o->state != WLR_DRM_OUTPUT_CONNECTED) {
			continue;
		}

		possible_crtc[i] = o->possible_crtc;
		crtc[o->crtc - drm->crtcs] = i;
	}

	possible_crtc[index] = output->possible_crtc;
	match_obj(drm->outputs->length, possible_crtc, drm->num_crtcs, crtc, crtc_res);

	realloc_planes(drm, crtc_res);

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (crtc_res[i] == UNMATCHED) {
			continue;
		}

		handled[crtc_res[i]] = true;

		if (crtc_res[i] != crtc[i]) {
			struct wlr_output_state *o = drm->outputs->items[crtc_res[i]];
			o->crtc = &drm->crtcs[i];
		}
	}

	for (size_t i = 0; i < drm->outputs->length; ++i) {
		if (!handled[i]) {
			wlr_drm_output_cleanup(drm->outputs->items[i], false);
		}
	}
}

static bool wlr_drm_output_set_mode(struct wlr_output_state *output,
		struct wlr_output_mode *mode) {
	struct wlr_backend_state *drm = wl_container_of(output->renderer, drm, renderer);

	wlr_log(L_INFO, "Modesetting '%s' with '%ux%u@%u mHz'", output->base->name,
			mode->width, mode->height, mode->refresh);

	drmModeConnector *conn = drmModeGetConnector(drm->fd, output->connector);
	if (!conn) {
		wlr_log_errno(L_ERROR, "Failed to get DRM connector");
		goto error_output;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "%s is not connected", output->base->name);
		goto error_output;
	}

	drmModeEncoder *enc = NULL;
	for (int i = 0; !enc && i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(drm->fd, conn->encoders[i]);
	}

	if (!enc) {
		wlr_log(L_ERROR, "Failed to get DRM encoder");
		goto error_conn;
	}

	output->possible_crtc = enc->possible_crtcs;
	realloc_crtcs(drm, output);

	if (!output->crtc) {
		wlr_log(L_ERROR, "Unable to match %s with a CRTC", output->base->name);
		goto error_enc;
	}

	struct wlr_drm_crtc *crtc = output->crtc;
	wlr_log(L_DEBUG, "%s: crtc=%ju ovr=%jd pri=%jd cur=%jd", output->base->name,
		crtc - drm->crtcs,
		crtc->overlay ? crtc->overlay - drm->overlay_planes : -1,
		crtc->primary ? crtc->primary - drm->primary_planes : -1,
		crtc->cursor ? crtc->cursor - drm->cursor_planes : -1);

	output->state = WLR_DRM_OUTPUT_CONNECTED;
	output->width = output->base->width = mode->width;
	output->height = output->base->height = mode->height;
	output->base->current_mode = mode;
	wl_signal_emit(&output->base->events.resolution, output->base);

	if (!plane_init_renderer(&drm->renderer, output->crtc->primary, mode)) {
		wlr_log(L_ERROR, "Failed to initalise renderer for plane");
		goto error_enc;
	}

	wlr_drm_output_start_renderer(output);

	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	return true;

error_enc:
	drmModeFreeEncoder(enc);
error_conn:
	drmModeFreeConnector(conn);
error_output:
	wlr_drm_output_cleanup(output, false);
	return false;
}

static void wlr_drm_output_transform(struct wlr_output_state *output,
		enum wl_output_transform transform) {
	output->base->transform = transform;
}

static bool wlr_drm_output_set_cursor(struct wlr_output_state *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	struct wlr_backend_state *state = wl_container_of(output->renderer, state, renderer);
	if (!buf) {
		drmModeSetCursor(state->fd, output->crtc->id, 0, 0, 0);
		return true;
	}

	if (!gbm_device_is_format_supported(state->renderer.gbm,
				GBM_FORMAT_ARGB8888, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE)) {
		wlr_log(L_ERROR, "Failed to create cursor bo: ARGB8888 pixel format is "
				"unsupported on this device");
		return false;
	}

	uint64_t bo_width, bo_height;
	int ret;

	ret = drmGetCap(state->fd, DRM_CAP_CURSOR_WIDTH, &bo_width);
	bo_width = ret ? 64 : bo_width;
	ret = drmGetCap(state->fd, DRM_CAP_CURSOR_HEIGHT, &bo_height);
	bo_height = ret ? 64 : bo_height;

	if (width > bo_width || height > bo_width) {
		wlr_log(L_INFO, "Cursor too large (max %dx%d)", (int)bo_width, (int)bo_height);
		return false;
	}

	for (int i = 0; i < 2; ++i) {
		if (output->cursor_bo[i]) {
			continue;
		}

		output->cursor_bo[i] = gbm_bo_create(state->renderer.gbm, bo_width, bo_height,
			GBM_FORMAT_ARGB8888, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);

		if (!output->cursor_bo[i]) {
			wlr_log(L_ERROR, "Failed to create cursor bo");
			return false;
		}
	}

	struct gbm_bo *bo;
	output->current_cursor ^= 1;
	bo = output->cursor_bo[output->current_cursor];

	uint32_t bo_stride = gbm_bo_get_stride(bo);
	uint8_t tmp[bo_stride * height];
	memset(tmp, 0, sizeof(tmp));

	for (size_t i = 0; i < height; ++i) {
		memcpy(tmp + i * bo_stride, buf + i * stride * 4, width * 4);
	}

	if (gbm_bo_write(bo, tmp, sizeof(tmp)) < 0) {
		wlr_log(L_ERROR, "Failed to write cursor to bo");
		return false;
	}

	uint32_t bo_handle = gbm_bo_get_handle(bo).u32;
	if (drmModeSetCursor(state->fd, output->crtc->id, bo_handle, bo_width, bo_height)) {
		wlr_log_errno(L_INFO, "Failed to set hardware cursor");
		return false;
	}

	return true;
}

static bool wlr_drm_output_move_cursor(struct wlr_output_state *output,
		int x, int y) {
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);
	return !drmModeMoveCursor(state->fd, output->crtc->id, x, y);
}

static void wlr_drm_output_destroy(struct wlr_output_state *output) {
	wlr_drm_output_cleanup(output, true);
	free(output);
}

static struct wlr_output_impl output_impl = {
	.enable = wlr_drm_output_enable,
	.set_mode = wlr_drm_output_set_mode,
	.transform = wlr_drm_output_transform,
	.set_cursor = wlr_drm_output_set_cursor,
	.move_cursor = wlr_drm_output_move_cursor,
	.destroy = wlr_drm_output_destroy,
	.make_current = wlr_drm_output_make_current,
	.swap_buffers = wlr_drm_output_swap_buffers,
};

static const int32_t subpixel_map[] = {
	[DRM_MODE_SUBPIXEL_UNKNOWN] = WL_OUTPUT_SUBPIXEL_UNKNOWN,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_RGB] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_BGR] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR,
	[DRM_MODE_SUBPIXEL_VERTICAL_RGB] = WL_OUTPUT_SUBPIXEL_VERTICAL_RGB,
	[DRM_MODE_SUBPIXEL_VERTICAL_BGR] = WL_OUTPUT_SUBPIXEL_VERTICAL_BGR,
	[DRM_MODE_SUBPIXEL_NONE] = WL_OUTPUT_SUBPIXEL_NONE,
};

void wlr_drm_scan_connectors(struct wlr_backend_state *drm) {
	wlr_log(L_INFO, "Scanning DRM connectors");

	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM resources");
		return;
	}

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *conn = drmModeGetConnector(drm->fd,
			res->connectors[i]);
		if (!conn) {
			wlr_log_errno(L_ERROR, "Failed to get DRM connector");
			continue;
		}

		struct wlr_output_state *output;
		int index = list_seq_find(drm->outputs, find_id, &conn->connector_id);

		if (index == -1) {
			output = calloc(1, sizeof(*output));
			if (!output) {
				wlr_log_errno(L_ERROR, "Allocation failed");
				drmModeFreeConnector(conn);
				continue;
			}

			output->base = wlr_output_create(&output_impl, output);
			if (!output->base) {
				wlr_log_errno(L_ERROR, "Allocation failed");
				drmModeFreeConnector(conn);
				free(output);
				continue;
			}

			output->renderer = &drm->renderer;
			output->state = WLR_DRM_OUTPUT_DISCONNECTED;
			output->connector = conn->connector_id;

			drmModeEncoder *curr_enc = drmModeGetEncoder(drm->fd, conn->encoder_id);
			if (curr_enc) {
				output->old_crtc = drmModeGetCrtc(drm->fd, curr_enc->crtc_id);
				drmModeFreeEncoder(curr_enc);
			}

			output->base->phys_width = conn->mmWidth;
			output->base->phys_height = conn->mmHeight;
			output->base->subpixel = subpixel_map[conn->subpixel];
			snprintf(output->base->name, sizeof(output->base->name), "%s-%"PRIu32,
				 conn_get_name(conn->connector_type),
				 conn->connector_type_id);

			wlr_drm_get_connector_props(drm->fd, output->connector, &output->props);

			size_t edid_len = 0;
			uint8_t *edid = wlr_drm_get_prop_blob(drm->fd, output->connector,
				output->props.edid, &edid_len);
			parse_edid(output->base, edid_len, edid);
			free(edid);

			wlr_output_create_global(output->base, drm->display);
			list_add(drm->outputs, output);
			wlr_log(L_INFO, "Found display '%s'", output->base->name);
		} else {
			output = drm->outputs->items[index];
		}

		if (output->state == WLR_DRM_OUTPUT_DISCONNECTED &&
				conn->connection == DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' connected", output->base->name);
			wlr_log(L_INFO, "Detected modes:");

			for (int i = 0; i < conn->count_modes; ++i) {
				struct wlr_output_mode_state *_state = calloc(1,
						sizeof(struct wlr_output_mode_state));
				_state->mode = conn->modes[i];
				struct wlr_output_mode *mode = calloc(1,
						sizeof(struct wlr_output_mode));
				mode->width = _state->mode.hdisplay;
				mode->height = _state->mode.vdisplay;
				mode->refresh = calculate_refresh_rate(&_state->mode);
				mode->state = _state;

				wlr_log(L_INFO, "  %"PRId32"@%"PRId32"@%"PRId32,
					mode->width, mode->height, mode->refresh);

				list_add(output->base->modes, mode);
			}

			output->state = WLR_DRM_OUTPUT_NEEDS_MODESET;
			wlr_log(L_INFO, "Sending modesetting signal for '%s'", output->base->name);
			wl_signal_emit(&drm->base->events.output_add, output->base);
		} else if (output->state == WLR_DRM_OUTPUT_CONNECTED &&
				conn->connection != DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' disconnected", output->base->name);
			wlr_drm_output_cleanup(output, false);
		}

		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, void *user) {
	struct wlr_output_state *output = user;
	struct wlr_backend_state *state =
		wl_container_of(output->renderer, state, renderer);

	struct wlr_drm_plane *plane = output->crtc->primary;
	if (plane->front) {
		gbm_surface_release_buffer(plane->gbm, plane->front);
		plane->front = NULL;
	}

	output->pageflip_pending = false;
	if (output->state == WLR_DRM_OUTPUT_CONNECTED && state->session->active) {
		wl_signal_emit(&output->base->events.frame, output->base);
	}
}

int wlr_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};

	drmHandleEvent(fd, &event);
	return 1;
}

static void restore_output(struct wlr_output_state *output, int fd) {
	// Wait for any pending pageflips to finish
	while (output->pageflip_pending) {
		wlr_drm_event(fd, 0, NULL);
	}

	drmModeCrtc *crtc = output->old_crtc;
	if (!crtc) {
		return;
	}

	drmModeSetCrtc(fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y,
		&output->connector, 1, &crtc->mode);
	drmModeFreeCrtc(crtc);
}

void wlr_drm_output_cleanup(struct wlr_output_state *output, bool restore) {
	if (!output) {
		return;
	}

	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_backend_state *state = wl_container_of(renderer, state, renderer);

	switch (output->state) {
	case WLR_DRM_OUTPUT_CONNECTED:
		output->state = WLR_DRM_OUTPUT_DISCONNECTED;
		if (restore) {
			restore_output(output, renderer->fd);
			restore = false;
		}

		output->crtc = NULL;
		output->possible_crtc = 0;
		/* Fallthrough */
	case WLR_DRM_OUTPUT_NEEDS_MODESET:
		output->state = WLR_DRM_OUTPUT_DISCONNECTED;
		if (restore) {
			restore_output(output, renderer->fd);
		}
		wlr_log(L_INFO, "Emmiting destruction signal for '%s'", output->base->name);
		wl_signal_emit(&state->base->events.output_remove, output->base);
		break;
	case WLR_DRM_OUTPUT_DISCONNECTED:
		break;
	}
}
