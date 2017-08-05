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

static const char *conn_name[] = {
	[DRM_MODE_CONNECTOR_Unknown]     = "Unknown",
	[DRM_MODE_CONNECTOR_VGA]         = "VGA",
	[DRM_MODE_CONNECTOR_DVII]        = "DVI-I",
	[DRM_MODE_CONNECTOR_DVID]        = "DVI-D",
	[DRM_MODE_CONNECTOR_DVIA]        = "DVI-A",
	[DRM_MODE_CONNECTOR_Composite]   = "Composite",
	[DRM_MODE_CONNECTOR_SVIDEO]      = "SVIDEO",
	[DRM_MODE_CONNECTOR_LVDS]        = "LVDS",
	[DRM_MODE_CONNECTOR_Component]   = "Component",
	[DRM_MODE_CONNECTOR_9PinDIN]     = "DIN",
	[DRM_MODE_CONNECTOR_DisplayPort] = "DP",
	[DRM_MODE_CONNECTOR_HDMIA]       = "HDMI-A",
	[DRM_MODE_CONNECTOR_HDMIB]       = "HDMI-B",
	[DRM_MODE_CONNECTOR_TV]          = "TV",
	[DRM_MODE_CONNECTOR_eDP]         = "eDP",
	[DRM_MODE_CONNECTOR_VIRTUAL]     = "Virtual",
	[DRM_MODE_CONNECTOR_DSI]         = "DSI",
};

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

	size_t num_planes = plane_res->count_planes;
	struct wlr_drm_plane *planes = calloc(num_planes, sizeof(*planes));
	if (!planes) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	size_t num_overlay = 0;
	size_t num_primary = 0;
	size_t num_cursor = 0;

	for (size_t i = 0; i < num_planes; ++i) {
		struct wlr_drm_plane *p = &planes[i];

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

		switch (type) {
		case DRM_PLANE_TYPE_OVERLAY:
			++num_overlay;
			break;
		case DRM_PLANE_TYPE_PRIMARY:
			++num_primary;
			break;
		case DRM_PLANE_TYPE_CURSOR:
			++num_cursor;
			break;
		}

		drmModeFreePlane(plane);
	}

	wlr_log(L_INFO, "(%zu overlay, %zu primary, %zu cursor)",
		num_overlay, num_primary, num_cursor);

	qsort(planes, num_planes, sizeof(*planes), cmp_plane);

	drm->num_planes = num_planes;
	drm->num_overlay_planes = num_overlay;
	drm->num_primary_planes = num_primary;
	drm->num_cursor_planes = num_cursor;

	drm->planes = planes;
	drm->overlay_planes = planes;
	drm->primary_planes = planes + num_overlay;
	drm->cursor_planes = planes + num_overlay + num_primary;

	return true;

error_planes:
	free(planes);
error_res:
	drmModeFreePlaneResources(plane_res);
	return false;
}

bool wlr_drm_init_resources(struct wlr_backend_state *drm) {
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
	if (!renderer) {
		return;
	}
	wlr_egl_free(&renderer->egl);
	gbm_device_destroy(renderer->gbm);
}

static void free_fb(struct gbm_bo *bo, void *data) {
	uint32_t *id = data;

	if (id && *id) {
		struct gbm_device *gbm = gbm_bo_get_device(bo);
		drmModeRmFB(gbm_device_get_fd(gbm), *id);
	}

	free(id);
}

static uint32_t get_fb_for_bo(struct gbm_bo *bo) {
	uint32_t *id = gbm_bo_get_user_data(bo);

	if (id) {
		return *id;
	}

	id = calloc(1, sizeof *id);
	if (!id) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return 0;
	}

	struct gbm_device *gbm = gbm_bo_get_device(bo);
	int fd = gbm_device_get_fd(gbm);

	drmModeAddFB(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), 24, 32,
		     gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, id);

	gbm_bo_set_user_data(bo, id, free_fb);

	return *id;
}

static void wlr_drm_output_make_current(struct wlr_output_state *output) {
	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_drm_plane *plane = output->crtc->primary;

	eglMakeCurrent(renderer->egl.display, plane->egl,
			plane->egl, renderer->egl.context);
}

static void wlr_drm_output_swap_buffers(struct wlr_output_state *output) {
	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_drm_crtc *crtc = output->crtc;
	struct wlr_drm_plane *plane = crtc->primary;

	if (!eglSwapBuffers(renderer->egl.display, plane->egl)) {
		return;
	}

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(plane->gbm);
	if (!bo) {
		return;
	}

	uint32_t fb_id = get_fb_for_bo(bo);
	drmModePageFlip(renderer->fd, crtc->id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, output);
	output->pageflip_pending = true;

	plane->front = plane->back;
	plane->back = bo;
}

void wlr_drm_output_pause_renderer(struct wlr_output_state *output) {
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	struct wlr_drm_plane *plane = output->crtc->primary;

	if (plane->front) {
		gbm_surface_release_buffer(plane->gbm, plane->front);
		plane->front = NULL;
	}
	if (plane->back) {
		gbm_surface_release_buffer(plane->gbm, plane->back);
		plane->back = NULL;
	}

}

void wlr_drm_output_start_renderer(struct wlr_output_state *output) {
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_output_mode *mode = output->base->current_mode;
	struct wlr_drm_crtc *crtc = output->crtc;
	struct wlr_drm_plane *plane = crtc->primary;

	eglMakeCurrent(renderer->egl.display, plane->egl, plane->egl, renderer->egl.context);

	glViewport(0, 0, output->width, output->height);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (!eglSwapBuffers(renderer->egl.display, plane->egl)) {
		return;
	}

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(plane->gbm);
	if (!bo) {
		return;
	}

	uint32_t fb_id = get_fb_for_bo(bo);
	drmModeSetCrtc(renderer->fd, crtc->id, fb_id, 0, 0,
		&output->connector, 1, &mode->state->mode);
	drmModePageFlip(renderer->fd, crtc->id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, output);
	output->pageflip_pending = true;

	plane->back = plane->front;
	plane->front = bo;
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
			DRM_MODE_DPMS_STANDBY);
	}
}

static inline bool is_taken(size_t n, const uint32_t arr[static n], uint32_t key)
{
	for (size_t i = 0; i < n; ++i) {
		if (arr[i] == key) {
			return true;
		}
	}
	return false;
}

/*
 * Store all of the non-recursive state in a struct, so we aren't literally
 * passing 12 arguments to a function.
 */
struct match_state {
	const size_t num_objs;
	const uint32_t *restrict objs;
	const size_t num_res;
	size_t score;
	size_t replaced;
	uint32_t *restrict res;
	uint32_t *restrict best;
	const uint32_t *restrict orig;
	bool exit_early;
};

enum {
	UNMATCHED = (uint32_t)-1,
	SKIP = (uint32_t)-2,
};

/*
 * skips: The number of SKIP elements encountered so far.
 * score: The number of resources we've matched so far.
 * replaced: The number of changes from the original solution.
 * i: The index of the current element.
 *
 * This tries to match a solution as close to st->orig as it can.
 *
 * Returns whether we've set a new best element with this solution.
 */
static bool match_obj_(struct match_state *st, size_t skips, size_t score, size_t replaced, size_t i) {
	// Finished
	if (i >= st->num_res) {
		if (score > st->score || (score == st->score && replaced < st->replaced)) {
			st->score = score;
			st->replaced = replaced;
			memcpy(st->best, st->res, sizeof st->best[0] * st->num_res);

			if (st->score == st->num_objs && st->replaced == 0) {
				st->exit_early = true;
			}
			st->exit_early = (st->score == st->num_res - skips
					|| st->score == st->num_objs)
					&& st->replaced == 0;

			return true;
		} else {
			return false;
		}
	}

	if (st->orig[i] == SKIP) {
		st->res[i] = SKIP;
		return match_obj_(st, skips + 1, score, replaced, i + 1);
	}

	/*
	 * Attempt to use the current solution first, to try and avoid
	 * recalculating everything
	 */

	if (st->orig[i] != UNMATCHED && !is_taken(i, st->res, st->orig[i])) {
		st->res[i] = st->orig[i];
		if (match_obj_(st, skips, score + 1, replaced, i + 1)) {
			return true;
		}
	}

	if (st->orig[i] != UNMATCHED) {
		++replaced;
	}

	bool is_best = false;
	for (st->res[i] = 0; st->res[i] < st->num_objs; ++st->res[i]) {
		// We tried this earlier
		if (st->res[i] == st->orig[i]) {
			continue;
		}

		// Not compatable
		if (!(st->objs[st->res[i]] & (1 << i))) {
			continue;
		}

		// Already taken
		if (is_taken(i, st->res, st->res[i])) {
			continue;
		}

		if (match_obj_(st, skips, score + 1, replaced, i + 1)) {
			is_best = true;
		}

		if (st->exit_early) {
			return true;
		}
	}

	if (is_best) {
		return true;
	}

	// Maybe this resource can't be matched
	st->res[i] = UNMATCHED;
	return match_obj_(st, skips, score, replaced, i + 1);
}

/*
 * Tries to match some DRM objects with some other DRM resource.
 * e.g. Match CRTCs with Encoders, CRTCs with Planes.
 *
 * objs contains a bit array which resources it can be matched with.
 * e.g. Bit 0 set means can be matched with res[0]
 *
 * res contains an index of which objs it is matched with or UNMATCHED.
 *
 * This solution is left in out.
 * Returns the total number of matched solutions.
 */
static size_t match_obj(size_t num_objs, const uint32_t objs[static restrict num_objs],
		size_t num_res, const uint32_t res[static restrict num_res],
		uint32_t out[static restrict num_res]) {
	uint32_t solution[num_res];

	struct match_state st = {
		.num_objs = num_objs,
		.num_res = num_res,
		.score = 0,
		.replaced = SIZE_MAX,
		.objs = objs,
		.res = solution,
		.best = out,
		.orig = res,
		.exit_early = false,
	};

	match_obj_(&st, 0, 0, 0, 0);
	return st.score;
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

static int32_t calculate_refresh_rate(drmModeModeInfo *mode) {
	int32_t refresh = (mode->clock * 1000000LL / mode->htotal +
		mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		refresh *= 2;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		refresh /= 2;
	}

	if (mode->vscan > 1) {
		refresh /= mode->vscan;
	}

	return refresh;
}

// Constructed from http://edid.tv/manufacturer
static const char *get_manufacturer(uint16_t id) {
#define ID(a, b, c) ((a & 0x1f) << 10) | ((b & 0x1f) << 5) | (c & 0x1f)
	switch (id) {
	case ID('A', 'A', 'A'): return "Avolites Ltd";
	case ID('A', 'C', 'I'): return "Ancor Communications Inc";
	case ID('A', 'C', 'R'): return "Acer Technologies";
	case ID('A', 'P', 'P'): return "Apple Computer Inc";
	case ID('B', 'N', 'O'): return "Bang & Olufsen";
	case ID('C', 'M', 'N'): return "Chimei Innolux Corporation";
	case ID('C', 'M', 'O'): return "Chi Mei Optoelectronics corp.";
	case ID('C', 'R', 'O'): return "Extraordinary Technologies PTY Limited";
	case ID('D', 'E', 'L'): return "Dell Inc.";
	case ID('D', 'O', 'N'): return "DENON, Ltd.";
	case ID('E', 'N', 'C'): return "Eizo Nanao Corporation";
	case ID('E', 'P', 'H'): return "Epiphan Systems Inc.";
	case ID('F', 'U', 'S'): return "Fujitsu Siemens Computers GmbH";
	case ID('G', 'S', 'M'): return "Goldstar Company Ltd";
	case ID('H', 'I', 'Q'): return "Kaohsiung Opto Electronics Americas, Inc.";
	case ID('H', 'S', 'D'): return "HannStar Display Corp";
	case ID('H', 'W', 'P'): return "Hewlett Packard";
	case ID('I', 'N', 'T'): return "Interphase Corporation";
	case ID('I', 'V', 'M'): return "Iiyama North America";
	case ID('L', 'E', 'N'): return "Lenovo Group Limited";
	case ID('M', 'A', 'X'): return "Rogen Tech Distribution Inc";
	case ID('M', 'E', 'G'): return "Abeam Tech Ltd";
	case ID('M', 'E', 'I'): return "Panasonic Industry Company";
	case ID('M', 'T', 'C'): return "Mars-Tech Corporation";
	case ID('M', 'T', 'X'): return "Matrox";
	case ID('N', 'E', 'C'): return "NEC Corporation";
	case ID('O', 'N', 'K'): return "ONKYO Corporation";
	case ID('O', 'R', 'N'): return "ORION ELECTRIC CO., LTD.";
	case ID('O', 'T', 'M'): return "Optoma Corporation";
	case ID('O', 'V', 'R'): return "Oculus VR, Inc.";
	case ID('P', 'H', 'L'): return "Philips Consumer Electronics Company";
	case ID('P', 'I', 'O'): return "Pioneer Electronic Corporation";
	case ID('P', 'N', 'R'): return "Planar Systems, Inc.";
	case ID('Q', 'D', 'S'): return "Quanta Display Inc.";
	case ID('S', 'A', 'M'): return "Samsung Electric Company";
	case ID('S', 'E', 'C'): return "Seiko Epson Corporation";
	case ID('S', 'H', 'P'): return "Sharp Corporation";
	case ID('S', 'I', 'I'): return "Silicon Image, Inc.";
	case ID('S', 'N', 'Y'): return "Sony";
	case ID('T', 'O', 'P'): return "Orion Communications Co., Ltd.";
	case ID('T', 'S', 'B'): return "Toshiba America Info Systems Inc";
	case ID('T', 'S', 'T'): return "Transtream Inc";
	case ID('U', 'N', 'K'): return "Unknown";
	case ID('V', 'I', 'Z'): return "VIZIO, Inc";
	case ID('V', 'S', 'C'): return "ViewSonic Corporation";
	case ID('Y', 'M', 'H'): return "Yamaha Corporation";
	default: return "Unknown";
	}
#undef ID
}

/* See https://en.wikipedia.org/wiki/Extended_Display_Identification_Data for layout of EDID data.
 * We don't parse the EDID properly. We just expect to receive valid data.
 */
static void parse_edid(struct wlr_output *restrict output, size_t len, const uint8_t *data) {
	if (!data || len < 128) {
		snprintf(output->make, sizeof(output->make), "<Unknown>");
		snprintf(output->model, sizeof(output->model), "<Unknown>");
		return;
	}

	uint16_t id = (data[8] << 8) | data[9];
	snprintf(output->make, sizeof(output->make), "%s", get_manufacturer(id));

	output->phys_width = ((data[68] & 0xf0) << 4) | data[66];
	output->phys_height = ((data[68] & 0x0f) << 8) | data[67];

	for (size_t i = 72; i <= 108; i += 18) {
		uint16_t flag = (data[i] << 8) | data[i + 1];
		if (flag == 0 && data[i + 3] == 0xFC) {
			sprintf(output->model, "%.13s", &data[i + 5]);

			// Monitor names are terminated by newline if they're too short
			char *nl = strchr(output->model, '\n');
			if (nl) {
				*nl = '\0';
			}

			break;
		}
	}
}

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
				 conn_name[conn->connector_type],
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
