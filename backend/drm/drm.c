#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>
#include "backend/drm.h"
#include "backend/drm-util.h"

bool wlr_drm_check_features(struct wlr_drm_backend *backend) {
	extern const struct wlr_drm_interface legacy_iface;
	extern const struct wlr_drm_interface atomic_iface;

	if (drmSetClientCap(backend->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		wlr_log(L_ERROR, "DRM universal planes unsupported");
		return false;
	}

	if (getenv("WLR_DRM_NO_ATOMIC")) {
		wlr_log(L_DEBUG, "WLR_DRM_NO_ATOMIC set, forcing legacy DRM interface");
		backend->iface = &legacy_iface;
	} else if (drmSetClientCap(backend->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		wlr_log(L_DEBUG, "Atomic modesetting unsupported, using legacy DRM interface");
		backend->iface = &legacy_iface;
	} else {
		wlr_log(L_DEBUG, "Using atomic DRM interface");
		backend->iface = &atomic_iface;
	}

	return true;
}

static int cmp_plane(const void *arg1, const void *arg2) {
	const struct wlr_drm_plane *a = arg1;
	const struct wlr_drm_plane *b = arg2;

	return (int)a->type - (int)b->type;
}

static bool init_planes(struct wlr_drm_backend *backend) {
	drmModePlaneRes *plane_res = drmModeGetPlaneResources(backend->fd);
	if (!plane_res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM plane resources");
		return false;
	}

	wlr_log(L_INFO, "Found %"PRIu32" DRM planes", plane_res->count_planes);

	if (plane_res->count_planes == 0) {
		drmModeFreePlaneResources(plane_res);
		return true;
	}

	backend->num_planes = plane_res->count_planes;
	backend->planes = calloc(backend->num_planes, sizeof(*backend->planes));
	if (!backend->planes) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < backend->num_planes; ++i) {
		struct wlr_drm_plane *p = &backend->planes[i];

		drmModePlane *plane = drmModeGetPlane(backend->fd, plane_res->planes[i]);
		if (!plane) {
			wlr_log_errno(L_ERROR, "Failed to get DRM plane");
			goto error_planes;
		}

		p->id = plane->plane_id;
		p->possible_crtcs = plane->possible_crtcs;
		uint64_t type;

		if (!wlr_drm_get_plane_props(backend->fd, p->id, &p->props) ||
				!wlr_drm_get_prop(backend->fd, p->id, p->props.type, &type)) {
			drmModeFreePlane(plane);
			goto error_planes;
		}

		p->type = type;
		backend->num_type_planes[type]++;

		drmModeFreePlane(plane);
	}

	wlr_log(L_INFO, "(%zu overlay, %zu primary, %zu cursor)",
		backend->num_overlay_planes,
		backend->num_primary_planes,
		backend->num_cursor_planes);

	qsort(backend->planes, backend->num_planes,
			sizeof(*backend->planes), cmp_plane);

	backend->overlay_planes = backend->planes;
	backend->primary_planes = backend->overlay_planes
		+ backend->num_overlay_planes;
	backend->cursor_planes = backend->primary_planes
		+ backend->num_primary_planes;

	drmModeFreePlaneResources(plane_res);
	return true;

error_planes:
	free(backend->planes);
error_res:
	drmModeFreePlaneResources(plane_res);
	return false;
}

bool wlr_drm_resources_init(struct wlr_drm_backend *backend) {
	drmModeRes *res = drmModeGetResources(backend->fd);
	if (!res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM resources");
		return false;
	}

	wlr_log(L_INFO, "Found %d DRM CRTCs", res->count_crtcs);

	backend->num_crtcs = res->count_crtcs;
	backend->crtcs = calloc(backend->num_crtcs, sizeof(backend->crtcs[0]));
	if (!backend->crtcs) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < backend->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &backend->crtcs[i];
		crtc->id = res->crtcs[i];
		wlr_drm_get_crtc_props(backend->fd, crtc->id, &crtc->props);
	}

	if (!init_planes(backend)) {
		goto error_crtcs;
	}

	drmModeFreeResources(res);

	return true;

error_crtcs:
	free(backend->crtcs);
error_res:
	drmModeFreeResources(res);
	return false;
}

void wlr_drm_resources_free(struct wlr_drm_backend *backend) {
	if (!backend) {
		return;
	}
	for (size_t i = 0; i < backend->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &backend->crtcs[i];
		drmModeAtomicFree(crtc->atomic);
		if (crtc->mode_id) {
			drmModeDestroyPropertyBlob(backend->fd, crtc->mode_id);
		}
	}
	free(backend->crtcs);
	free(backend->planes);
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

static bool wlr_drm_plane_renderer_init(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane, uint32_t width, uint32_t height, uint32_t format, uint32_t flags) {
	if (plane->width == width && plane->height == height) {
		return true;
	}

	plane->width = width;
	plane->height = height;

	plane->gbm = gbm_surface_create(renderer->gbm, width, height,
		format, GBM_BO_USE_RENDERING | flags);
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

static void wlr_drm_plane_renderer_free(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane) {
	if (!renderer || !plane) {
		return;
	}

	eglMakeCurrent(renderer->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (plane->front) {
		gbm_surface_release_buffer(plane->gbm, plane->front);
	}
	if (plane->back) {
		gbm_surface_release_buffer(plane->gbm, plane->back);
	}

	if (plane->egl) {
		eglDestroySurface(renderer->egl.display, plane->egl);
	}
	if (plane->gbm) {
		gbm_surface_destroy(plane->gbm);
	}

	if (plane->wlr_tex) {
		wlr_texture_destroy(plane->wlr_tex);
	}
	if (plane->wlr_rend) {
		wlr_renderer_destroy(plane->wlr_rend);
	}
	if (plane->cursor_bo) {
		gbm_bo_destroy(plane->cursor_bo);
	}

	plane->width = 0;
	plane->height = 0;
	plane->egl = EGL_NO_SURFACE;
	plane->gbm = NULL;
	plane->front = NULL;
	plane->back = NULL;
	plane->wlr_rend = NULL;
	plane->wlr_tex = NULL;
	plane->cursor_bo = NULL;
}

static void wlr_drm_plane_make_current(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane) {
	eglMakeCurrent(renderer->egl.display, plane->egl, plane->egl,
		renderer->egl.context);
}

static void wlr_drm_plane_swap_buffers(struct wlr_drm_renderer *renderer,
		struct wlr_drm_plane *plane) {
	if (plane->front) {
		gbm_surface_release_buffer(plane->gbm, plane->front);
	}

	eglSwapBuffers(renderer->egl.display, plane->egl);

	plane->front = plane->back;
	plane->back = gbm_surface_lock_front_buffer(plane->gbm);
}

static void wlr_drm_output_make_current(struct wlr_output *_output) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	wlr_drm_plane_make_current(output->renderer, output->crtc->primary);
}

static void wlr_drm_output_swap_buffers(struct wlr_output *_output) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	struct wlr_drm_backend *backend =
		wl_container_of(output->renderer, backend, renderer);
	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_drm_crtc *crtc = output->crtc;
	struct wlr_drm_plane *plane = crtc->primary;

	wlr_drm_plane_swap_buffers(renderer, plane);

	uint32_t fb_id = get_fb_for_bo(plane->back);

	if (backend->iface->crtc_pageflip(backend, output, crtc, fb_id, NULL)) {
		output->pageflip_pending = true;
	} else {
		wl_event_source_timer_update(output->retry_pageflip,
			output->output.current_mode->refresh);
	}
}

static void wlr_drm_output_set_gamma(struct wlr_output *_output,
		uint16_t size, uint16_t *r, uint16_t *g, uint16_t *b) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	struct wlr_drm_backend *backend =
		wl_container_of(output->renderer, backend, renderer);
	drmModeCrtcSetGamma(backend->fd, output->crtc->id, size, r, g, b);
}

static uint16_t wlr_drm_output_get_gamma_size(struct wlr_output *_output) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	drmModeCrtc *crtc = output->old_crtc;
	if (!crtc) {
		return 0;
	}
	return crtc->gamma_size;
}

void wlr_drm_output_start_renderer(struct wlr_drm_output *output) {
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	struct wlr_drm_backend *backend =
		wl_container_of(output->renderer, backend, renderer);
	struct wlr_drm_renderer *renderer = output->renderer;
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

	struct wlr_drm_output_mode *_mode =
		(struct wlr_drm_output_mode *)output->output.current_mode;
	drmModeModeInfo *mode = &_mode->mode;
	if (backend->iface->crtc_pageflip(backend, output, crtc, get_fb_for_bo(bo), mode)) {
		output->pageflip_pending = true;
	} else {
		wl_event_source_timer_update(output->retry_pageflip,
			output->output.current_mode->refresh);
	}
}

static void wlr_drm_output_enable(struct wlr_output *_output, bool enable) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	struct wlr_drm_backend *backend =
		wl_container_of(output->renderer, backend, renderer);
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	backend->iface->conn_enable(backend, output, enable);

	if (enable) {
		wlr_drm_output_start_renderer(output);
	}
}

static void realloc_planes(struct wlr_drm_backend *backend, const uint32_t *crtc_in) {
	// overlay, primary, cursor
	for (int type = 0; type < 3; ++type) {
		if (backend->num_type_planes[type] == 0) {
			continue;
		}

		uint32_t possible[backend->num_type_planes[type]];
		uint32_t crtc[backend->num_crtcs];
		uint32_t crtc_res[backend->num_crtcs];

		for (size_t i = 0; i < backend->num_type_planes[type]; ++i) {
			possible[i] = backend->type_planes[type][i].possible_crtcs;
		}

		for (size_t i = 0; i < backend->num_crtcs; ++i) {
			if (crtc_in[i] == UNMATCHED) {
				crtc[i] = SKIP;
			} else if (backend->crtcs[i].planes[type]) {
				crtc[i] = backend->crtcs[i].planes[type]
					- backend->type_planes[type];
			} else {
				crtc[i] = UNMATCHED;
			}
		}

		match_obj(backend->num_type_planes[type], possible,
				backend->num_crtcs, crtc, crtc_res);

		for (size_t i = 0; i < backend->num_crtcs; ++i) {
			if (crtc_res[i] == UNMATCHED || crtc_res[i] == SKIP) {
				continue;
			}

			struct wlr_drm_crtc *c = &backend->crtcs[i];
			struct wlr_drm_plane **old = &c->planes[type];
			struct wlr_drm_plane *new = &backend->type_planes[type][crtc_res[i]];

			if (*old != new) {
				wlr_drm_plane_renderer_free(&backend->renderer, *old);
				wlr_drm_plane_renderer_free(&backend->renderer, new);
				*old = new;
			}
		}
	}
}

static void realloc_crtcs(struct wlr_drm_backend *backend,
		struct wlr_drm_output *output) {
	uint32_t crtc[backend->num_crtcs];
	uint32_t crtc_res[backend->num_crtcs];
	uint32_t possible_crtc[backend->outputs->length];

	for (size_t i = 0; i < backend->num_crtcs; ++i) {
		crtc[i] = UNMATCHED;
	}

	memset(possible_crtc, 0, sizeof(possible_crtc));

	ssize_t index = -1;
	for (size_t i = 0; i < backend->outputs->length; ++i) {
		struct wlr_drm_output *o = backend->outputs->items[i];
		if (o == output) {
			index = i;
		}

		if (o->state != WLR_DRM_OUTPUT_CONNECTED) {
			continue;
		}

		possible_crtc[i] = o->possible_crtc;
		crtc[o->crtc - backend->crtcs] = i;
	}
	assert(index != -1);

	possible_crtc[index] = output->possible_crtc;
	match_obj(backend->outputs->length, possible_crtc,
			backend->num_crtcs, crtc, crtc_res);

	bool matched = false;
	for (size_t i = 0; i < backend->num_crtcs; ++i) {
		// We don't want any of the current monitors to be deactivated.
		if (crtc[i] != UNMATCHED && crtc_res[i] == UNMATCHED) {
			return;
		}
		if (crtc_res[i] == index) {
			matched = true;
		}
	}

	// There is no point doing anything if this monitor doesn't get activated
	if (!matched) {
		return;
	}

	for (size_t i = 0; i < backend->num_crtcs; ++i) {
		if (crtc_res[i] == UNMATCHED) {
			continue;
		}

		if (crtc_res[i] != crtc[i]) {
			struct wlr_drm_output *o = backend->outputs->items[crtc_res[i]];
			o->crtc = &backend->crtcs[i];
		}
	}

	realloc_planes(backend, crtc_res);
}

static bool wlr_drm_output_set_mode(struct wlr_output *_output,
		struct wlr_output_mode *mode) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	struct wlr_drm_backend *backend
		= wl_container_of(output->renderer, backend, renderer);

	wlr_log(L_INFO, "Modesetting '%s' with '%ux%u@%u mHz'", output->output.name,
			mode->width, mode->height, mode->refresh);

	drmModeConnector *conn = drmModeGetConnector(backend->fd, output->connector);
	if (!conn) {
		wlr_log_errno(L_ERROR, "Failed to get DRM connector");
		goto error_output;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "%s is not connected", output->output.name);
		goto error_output;
	}

	drmModeEncoder *enc = NULL;
	for (int i = 0; !enc && i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(backend->fd, conn->encoders[i]);
	}

	if (!enc) {
		wlr_log(L_ERROR, "Failed to get DRM encoder");
		goto error_conn;
	}

	output->possible_crtc = enc->possible_crtcs;
	realloc_crtcs(backend, output);

	if (!output->crtc) {
		wlr_log(L_ERROR, "Unable to match %s with a CRTC", output->output.name);
		goto error_enc;
	}

	struct wlr_drm_crtc *crtc = output->crtc;
	wlr_log(L_DEBUG, "%s: crtc=%ju ovr=%jd pri=%jd cur=%jd", output->output.name,
		crtc - backend->crtcs,
		crtc->overlay ? crtc->overlay - backend->overlay_planes : -1,
		crtc->primary ? crtc->primary - backend->primary_planes : -1,
		crtc->cursor ? crtc->cursor - backend->cursor_planes : -1);

	output->state = WLR_DRM_OUTPUT_CONNECTED;
	output->width = output->output.width = mode->width;
	output->height = output->output.height = mode->height;
	output->output.current_mode = mode;
	wl_signal_emit(&output->output.events.resolution, &output->output);

	// Since realloc_crtcs can deallocate planes on OTHER outputs,
	// we actually need to reinitalise all of them
	for (size_t i = 0; i < backend->outputs->length; ++i) {
		struct wlr_drm_output *output = backend->outputs->items[i];
		struct wlr_output_mode *mode = output->output.current_mode;
		struct wlr_drm_crtc *crtc = output->crtc;

		if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
			continue;
		}

		if (!wlr_drm_plane_renderer_init(&backend->renderer, crtc->primary,
				mode->width, mode->height, GBM_FORMAT_XRGB8888,
				GBM_BO_USE_SCANOUT)) {
			wlr_log(L_ERROR, "Failed to initalise renderer for plane");
			goto error_enc;
		}

		wlr_drm_output_start_renderer(output);
	}

	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	return true;

error_enc:
	drmModeFreeEncoder(enc);
error_conn:
	drmModeFreeConnector(conn);
error_output:
	wlr_drm_output_cleanup(output);
	return false;
}

static void wlr_drm_output_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->transform = transform;
}

static bool wlr_drm_output_set_cursor(struct wlr_output *_output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	struct wlr_drm_backend *backend
		= wl_container_of(output->renderer, backend, renderer);
	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_drm_crtc *crtc = output->crtc;
	struct wlr_drm_plane *plane = crtc->cursor;

	if (!buf) {
		return backend->iface->crtc_set_cursor(backend, crtc, NULL);
	}

	// We don't have a real cursor plane, so we make a fake one
	if (!plane) {
		plane = calloc(1, sizeof(*plane));
		if (!plane) {
			wlr_log_errno(L_ERROR, "Allocation failed");
			return false;
		}
		crtc->cursor = plane;
	}

	if (!plane->gbm) {
		int ret;
		uint64_t w, h;
		ret = drmGetCap(backend->fd, DRM_CAP_CURSOR_WIDTH, &w);
		w = ret ? 64 : w;
		ret = drmGetCap(backend->fd, DRM_CAP_CURSOR_HEIGHT, &h);
		h = ret ? 64 : h;

		if (width > w || height > h) {
			wlr_log(L_INFO, "Cursor too large (max %dx%d)", (int)w, (int)h);
			return false;
		}

		if (!wlr_drm_plane_renderer_init(renderer, plane, w, h, GBM_FORMAT_ARGB8888, 0)) {
			wlr_log(L_ERROR, "Cannot allocate cursor resources");
			return false;
		}

		plane->cursor_bo = gbm_bo_create(renderer->gbm, w, h, GBM_FORMAT_ARGB8888,
			GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
		if (!plane->cursor_bo) {
			wlr_log_errno(L_ERROR, "Failed to create cursor bo");
			return false;
		}

		// OpenGL will read the pixels out upside down,
		// so we need to flip the image vertically
		wlr_matrix_texture(plane->matrix, plane->width, plane->height,
			output->output.transform ^ WL_OUTPUT_TRANSFORM_FLIPPED_180);

		// TODO the image needs to be rotated depending on the output rotation

		plane->wlr_rend = wlr_gles2_renderer_create(&backend->backend);
		if (!plane->wlr_rend) {
			return false;
		}

		plane->wlr_tex = wlr_render_texture_create(plane->wlr_rend);
		if (!plane->wlr_tex) {
			return false;
		}
	}

	struct gbm_bo *bo = plane->cursor_bo;
	uint32_t bo_width = gbm_bo_get_width(bo);
	uint32_t bo_height = gbm_bo_get_height(bo);
	uint32_t bo_stride;
	void *bo_data;

	if (!gbm_bo_map(bo, 0, 0, bo_width, bo_height,
			GBM_BO_TRANSFER_WRITE, &bo_stride, &bo_data)) {
		wlr_log_errno(L_ERROR, "Unable to map buffer");
		return false;
	}

	wlr_drm_plane_make_current(renderer, plane);

	wlr_texture_upload_pixels(plane->wlr_tex, WL_SHM_FORMAT_ARGB8888,
		stride, width, height, buf);

	glViewport(0, 0, plane->width, plane->height);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	float matrix[16];
	wlr_texture_get_matrix(plane->wlr_tex, &matrix, &plane->matrix, 0, 0);
	wlr_render_with_matrix(plane->wlr_rend, plane->wlr_tex, &matrix);

	glFinish();
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, bo_stride);
	glReadPixels(0, 0, plane->width, plane->height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bo_data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	wlr_drm_plane_swap_buffers(renderer, plane);

	gbm_bo_unmap(bo, bo_data);

	return backend->iface->crtc_set_cursor(backend, crtc, bo);
}

static bool wlr_drm_output_move_cursor(struct wlr_output *_output,
		int x, int y) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	struct wlr_drm_backend *backend =
		wl_container_of(output->renderer, backend, renderer);

	int width, height, tmp;
	wlr_output_effective_resolution(_output, &width, &height);

	switch (_output->transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		// nothing to do
		break;
	case WL_OUTPUT_TRANSFORM_270:
		tmp = x;
		x = y;
		y = -(tmp - width);
		break;
	case WL_OUTPUT_TRANSFORM_90:
		tmp = x;
		x = -(y - height);
		y = tmp;
		break;
	default:
		// TODO other transformations
		wlr_log(L_ERROR, "TODO: handle surface to crtc for transformation = %d",
			_output->transform);
		break;
	}

	return backend->iface->crtc_move_cursor(backend, output->crtc, x, y);
}

static void wlr_drm_output_destroy(struct wlr_output *_output) {
	struct wlr_drm_output *output = (struct wlr_drm_output *)_output;
	wlr_drm_output_cleanup(output);
	wl_event_source_remove(output->retry_pageflip);
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
	.set_gamma = wlr_drm_output_set_gamma,
	.get_gamma_size = wlr_drm_output_get_gamma_size,
};

static int retry_pageflip(void *data) {
	struct wlr_drm_output *output = data;
	wlr_log(L_INFO, "%s: Retrying pageflip", output->output.name);
	wlr_drm_output_start_renderer(output);
	return 0;
}

static int find_id(const void *item, const void *cmp_to) {
	const struct wlr_drm_output *output = item;
	const uint32_t *id = cmp_to;

	if (output->connector < *id) {
		return -1;
	} else if (output->connector > *id) {
		return 1;
	} else {
		return 0;
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

void wlr_drm_scan_connectors(struct wlr_drm_backend *backend) {
	wlr_log(L_INFO, "Scanning DRM connectors");

	drmModeRes *res = drmModeGetResources(backend->fd);
	if (!res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM resources");
		return;
	}

	size_t seen_len = backend->outputs->length;
	// +1 so it can never be 0
	bool seen[seen_len + 1];
	memset(seen, 0, sizeof(seen));

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *conn = drmModeGetConnector(backend->fd,
			res->connectors[i]);
		if (!conn) {
			wlr_log_errno(L_ERROR, "Failed to get DRM connector");
			continue;
		}

		struct wlr_drm_output *output;
		int index = list_seq_find(backend->outputs, find_id, &conn->connector_id);

		if (index == -1) {
			output = calloc(1, sizeof(*output));
			if (!output) {
				wlr_log_errno(L_ERROR, "Allocation failed");
				drmModeFreeConnector(conn);
				continue;
			}
			wlr_output_init(&output->output, &output_impl);

			struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
			output->retry_pageflip = wl_event_loop_add_timer(ev, retry_pageflip,
				output);


			output->renderer = &backend->renderer;
			output->state = WLR_DRM_OUTPUT_DISCONNECTED;
			output->connector = conn->connector_id;

			drmModeEncoder *curr_enc = drmModeGetEncoder(backend->fd,
					conn->encoder_id);
			if (curr_enc) {
				output->old_crtc = drmModeGetCrtc(backend->fd, curr_enc->crtc_id);
				drmModeFreeEncoder(curr_enc);
			}

			output->output.phys_width = conn->mmWidth;
			output->output.phys_height = conn->mmHeight;
			output->output.subpixel = subpixel_map[conn->subpixel];
			snprintf(output->output.name, sizeof(output->output.name), "%s-%"PRIu32,
				 conn_get_name(conn->connector_type),
				 conn->connector_type_id);

			wlr_drm_get_connector_props(backend->fd,
					output->connector, &output->props);

			size_t edid_len = 0;
			uint8_t *edid = wlr_drm_get_prop_blob(backend->fd,
				output->connector, output->props.edid, &edid_len);
			parse_edid(&output->output, edid_len, edid);
			free(edid);

			if (list_add(backend->outputs, output) == -1) {
				wlr_log_errno(L_ERROR, "Allocation failed");
				drmModeFreeConnector(conn);
				wl_event_source_remove(output->retry_pageflip);
				free(output);
				continue;
			}
			wlr_output_create_global(&output->output, backend->display);
			wlr_log(L_INFO, "Found display '%s'", output->output.name);
		} else {
			output = backend->outputs->items[index];
			seen[index] = true;
		}

		if (output->state == WLR_DRM_OUTPUT_DISCONNECTED &&
				conn->connection == DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' connected", output->output.name);
			wlr_log(L_INFO, "Detected modes:");

			for (int i = 0; i < conn->count_modes; ++i) {
				struct wlr_drm_output_mode *mode = calloc(1,
					sizeof(struct wlr_drm_output_mode));
				if (!mode) {
					wlr_log_errno(L_ERROR, "Allocation failed");
					continue;
				}
				mode->mode = conn->modes[i];
				mode->wlr_mode.width = mode->mode.hdisplay;
				mode->wlr_mode.height = mode->mode.vdisplay;
				mode->wlr_mode.refresh = calculate_refresh_rate(&mode->mode);

				wlr_log(L_INFO, "  %"PRId32"@%"PRId32"@%"PRId32,
					mode->wlr_mode.width, mode->wlr_mode.height,
					mode->wlr_mode.refresh);

				if (list_add(output->output.modes, mode) == -1) {
					wlr_log_errno(L_ERROR, "Allocation failed");
					free(mode);
					continue;
				}
			}

			output->state = WLR_DRM_OUTPUT_NEEDS_MODESET;
			wlr_log(L_INFO, "Sending modesetting signal for '%s'", output->output.name);
			wl_signal_emit(&backend->backend.events.output_add, &output->output);
		} else if (output->state == WLR_DRM_OUTPUT_CONNECTED &&
				conn->connection != DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' disconnected", output->output.name);
			wlr_drm_output_cleanup(output);
		}

		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);

	for (size_t i = seen_len; i-- > 0;) {
		if (seen[i]) {
			continue;
		}

		struct wlr_drm_output *output = backend->outputs->items[i];

		wlr_log(L_INFO, "'%s' disappeared", output->output.name);
		wlr_drm_output_cleanup(output);

		drmModeFreeCrtc(output->old_crtc);
		wl_event_source_remove(output->retry_pageflip);
		free(output);

		list_del(backend->outputs, i);
	}
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, void *user) {
	struct wlr_drm_output *output = user;
	struct wlr_drm_backend *backend =
		wl_container_of(output->renderer, backend, renderer);

	output->pageflip_pending = false;
	if (output->state != WLR_DRM_OUTPUT_CONNECTED) {
		return;
	}

	struct wlr_drm_plane *plane = output->crtc->primary;
	if (plane->front) {
		gbm_surface_release_buffer(plane->gbm, plane->front);
		plane->front = NULL;
	}

	if (backend->session->active) {
		wl_signal_emit(&output->output.events.frame, &output->output);
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

void wlr_drm_restore_outputs(struct wlr_drm_backend *drm) {
	uint64_t to_close = (1 << drm->outputs->length) - 1;

	for (size_t i = 0; i < drm->outputs->length; ++i) {
		struct wlr_drm_output *output = drm->outputs->items[i];
		if (output->state == WLR_DRM_OUTPUT_CONNECTED) {
			output->state = WLR_DRM_OUTPUT_CLEANUP;
		}
	}

	time_t timeout = time(NULL) + 5;

	while (to_close && time(NULL) < timeout) {
		wlr_drm_event(drm->fd, 0, NULL);
		for (size_t i = 0; i < drm->outputs->length; ++i) {
			struct wlr_drm_output *output = drm->outputs->items[i];
			if (output->state != WLR_DRM_OUTPUT_CLEANUP || !output->pageflip_pending) {
				to_close &= ~(1 << i);
			}
		}
	}

	if (to_close) {
		wlr_log(L_ERROR, "Timed out stopping output renderers");
	}

	for (size_t i = 0; i < drm->outputs->length; ++i) {
		struct wlr_drm_output *output = drm->outputs->items[i];
		drmModeCrtc *crtc = output->old_crtc;
		if (!crtc) {
			continue;
		}

		drmModeSetCrtc(drm->fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y,
			&output->connector, 1, &crtc->mode);
		drmModeFreeCrtc(crtc);
	}
}

void wlr_drm_output_cleanup(struct wlr_drm_output *output) {
	if (!output) {
		return;
	}

	struct wlr_drm_renderer *renderer = output->renderer;
	struct wlr_drm_backend *backend =
		wl_container_of(renderer, backend, renderer);

	switch (output->state) {
	case WLR_DRM_OUTPUT_CONNECTED:
	case WLR_DRM_OUTPUT_CLEANUP:;
		struct wlr_drm_crtc *crtc = output->crtc;
		for (int i = 0; i < 3; ++i) {
			wlr_drm_plane_renderer_free(renderer, crtc->planes[i]);
			if (crtc->planes[i] && crtc->planes[i]->id == 0) {
				free(crtc->planes[i]);
				crtc->planes[i] = NULL;
			}
		}

		output->crtc = NULL;
		output->possible_crtc = 0;
		/* Fallthrough */
	case WLR_DRM_OUTPUT_NEEDS_MODESET:
		wlr_log(L_INFO, "Emmiting destruction signal for '%s'",
				output->output.name);
		wl_signal_emit(&backend->backend.events.output_remove, &output->output);
		break;
	case WLR_DRM_OUTPUT_DISCONNECTED:
		break;
	}
}
