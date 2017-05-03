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

#include "backend/drm/backend.h"
#include "backend/drm/drm.h"
#include "common/log.h"

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
		drmModeRmFB(gbm_bo_get_fd(bo), *id);
	}

	free(id);
}

static uint32_t get_fb_for_bo(int fd, struct gbm_bo *bo) {
	uint32_t *id = gbm_bo_get_user_data(bo);

	if (id) {
		return *id;
	}

	id = calloc(1, sizeof *id);
	if (!id) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return 0;
	}

	drmModeAddFB(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo), 24, 32,
		     gbm_bo_get_stride(bo), gbm_bo_get_handle(bo).u32, id);

	gbm_bo_set_user_data(bo, id, free_fb);

	return *id;
}

void wlr_drm_output_begin(struct wlr_drm_output *out) {
	struct wlr_drm_renderer *renderer = out->renderer;
	eglMakeCurrent(renderer->egl.display, out->egl, out->egl, renderer->egl.context);
}

void wlr_drm_output_end(struct wlr_drm_output *out) {
	struct wlr_drm_renderer *renderer = out->renderer;
	eglSwapBuffers(renderer->egl.display, out->egl);

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(out->gbm);
	uint32_t fb_id = get_fb_for_bo(renderer->fd, bo);

	drmModePageFlip(renderer->fd, out->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT, out);

	gbm_surface_release_buffer(out->gbm, bo);

	out->pageflip_pending = false;
}

static bool display_init_renderer(struct wlr_drm_renderer *renderer,
	struct wlr_drm_output *out) {

	out->renderer = renderer;

	out->gbm = gbm_surface_create(renderer->gbm, out->width, out->height,
		GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!out->gbm) {
		wlr_log(L_ERROR, "Failed to create GBM surface for %s: %s", out->name,
			strerror(errno));
		return false;
	}

	out->egl = wlr_egl_create_surface(&renderer->egl, out->gbm);
	if (out->egl == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface for %s", out->name);
		return false;
	}

	// Render black frame

	eglMakeCurrent(renderer->egl.display, out->egl, out->egl, renderer->egl.context);

	glViewport(0, 0, out->width, out->height);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(renderer->egl.display, out->egl);

	struct gbm_bo *bo = gbm_surface_lock_front_buffer(out->gbm);
	uint32_t fb_id = get_fb_for_bo(renderer->fd, bo);

	drmModeSetCrtc(renderer->fd, out->crtc, fb_id, 0, 0,
		       &out->connector, 1, &out->active_mode->mode);
	drmModePageFlip(renderer->fd, out->crtc, fb_id, DRM_MODE_PAGE_FLIP_EVENT, out);

	gbm_surface_release_buffer(out->gbm, bo);

	return true;
}

static int find_id(const void *item, const void *cmp_to) {
	const struct wlr_drm_output *out = item;
	const uint32_t *id = cmp_to;

	if (out->connector < *id) {
		return -1;
	} else if (out->connector > *id) {
		return 1;
	} else {
		return 0;
	}
}

void wlr_drm_scan_connectors(struct wlr_drm_backend *backend) {
	drmModeRes *res = drmModeGetResources(backend->fd);
	if (!res) {
		wlr_log(L_ERROR, "Failed to get DRM resources");
		return;
	}

	for (int i = 0; i < res->count_connectors; ++i) {
		uint32_t id = res->connectors[i];

		drmModeConnector *conn = drmModeGetConnector(backend->fd, id);
		if (!conn) {
			wlr_log(L_ERROR, "Failed to get DRM connector");
			continue;
		}

		struct wlr_drm_output *out;
		int index = list_seq_find(backend->outputs, find_id, &id);

		if (index == -1) {
			out = calloc(1, sizeof *out);
			if (!out) {
				wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
				drmModeFreeConnector(conn);
				continue;
			}

			out->renderer = &backend->renderer;
			out->state = DRM_OUTPUT_DISCONNECTED;
			out->connector = res->connectors[i];
			snprintf(out->name, sizeof out->name, "%s-%"PRIu32,
				 conn_name[conn->connector_type],
				 conn->connector_type_id);

			list_add(backend->outputs, out);
			wlr_log(L_INFO, "Found display '%s'", out->name);
		} else {
			out = backend->outputs->items[index];
		}

		if (out->state == DRM_OUTPUT_DISCONNECTED &&
			conn->connection == DRM_MODE_CONNECTED) {

			wlr_log(L_INFO, "'%s' connected", out->name);

			out->modes = malloc(sizeof(*out->modes) * conn->count_modes);
			if (!out->modes) {
				wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
				goto error;
			}

			wlr_log(L_INFO, "Detected modes:");

			out->num_modes = conn->count_modes;
			for (int i = 0; i < conn->count_modes; ++i) {
				drmModeModeInfo *mode = &conn->modes[i];
				out->modes[i].width = mode->hdisplay;
				out->modes[i].height = mode->vdisplay;
				// TODO: Calculate more accurate refresh rate
				out->modes[i].rate = mode->vrefresh;
				out->modes[i].mode = *mode;

				wlr_log(L_INFO, "  %"PRIu16"@%"PRIu16"@%"PRIu32,
					mode->hdisplay, mode->vdisplay,
					mode->vrefresh);
			}

			out->state = DRM_OUTPUT_NEEDS_MODESET;
			wlr_log(L_INFO, "Sending modesetting signal for '%s'", out->name);
			wl_signal_emit(&backend->signals.output_add, out);

		} else if (out->state == DRM_OUTPUT_CONNECTED &&
			conn->connection != DRM_MODE_CONNECTED) {

			wlr_drm_output_cleanup(out, false);
			wlr_log(L_INFO, "Sending destruction signal for '%s'", out->name);
			wl_signal_emit(&backend->signals.output_rem, out);
		}
error:
		drmModeFreeConnector(conn);
	}

	drmModeFreeResources(res);
}

struct wlr_drm_mode *wlr_drm_output_get_modes(struct wlr_drm_output *out, size_t *count) {
	if (out->state == DRM_OUTPUT_DISCONNECTED) {
		*count = 0;
		return NULL;
	}

	*count = out->num_modes;
	return out->modes;
}

bool wlr_drm_output_modeset(struct wlr_drm_output *out, struct wlr_drm_mode *mode) {
	struct wlr_drm_backend *backend = wl_container_of(out->renderer, backend, renderer);

	wlr_log(L_INFO, "Modesetting '%s' with '%ux%u@%u'", out->name, mode->width,
		mode->height, mode->rate);

	drmModeConnector *conn = drmModeGetConnector(backend->fd, out->connector);
	if (!conn) {
		wlr_log(L_ERROR, "Failed to get DRM connector");
		goto error;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "%s is not connected", out->name);
		goto error;
	}

	drmModeEncoder *curr_enc = drmModeGetEncoder(backend->fd, conn->encoder_id);
	if (curr_enc) {
		out->old_crtc = drmModeGetCrtc(backend->fd, curr_enc->crtc_id);
		free(curr_enc);
	}

	drmModeRes *res = drmModeGetResources(backend->fd);
	if (!res) {
		wlr_log(L_ERROR, "Failed to get DRM resources");
		goto error;
	}

	bool success = false;
	for (int i = 0; !success && i < conn->count_encoders; ++i) {
		drmModeEncoder *enc = drmModeGetEncoder(backend->fd, conn->encoders[i]);
		if (!enc)
			continue;

		for (int j = 0; j < res->count_crtcs; ++j) {
			if ((enc->possible_crtcs & (1 << j)) == 0) {
				continue;
			}

			if ((backend->taken_crtcs & (1 << j)) == 0) {
				backend->taken_crtcs |= 1 << j;
				out->crtc = res->crtcs[j];

				success = true;
				break;
			}
		}

		drmModeFreeEncoder(enc);
	}

	drmModeFreeResources(res);

	if (!success) {
		wlr_log(L_ERROR, "Failed to find CRTC for %s", out->name);
		goto error;
	}

	out->state = DRM_OUTPUT_CONNECTED;
	out->active_mode = mode;
	out->width = mode->width;
	out->height = mode->height;

	if (!display_init_renderer(&backend->renderer, out)) {
		wlr_log(L_ERROR, "Failed to initalise renderer for %s", out->name);
		goto error;
	}

	drmModeFreeConnector(conn);

	return true;

error:
	out->state = DRM_OUTPUT_DISCONNECTED;
	drmModeFreeConnector(conn);
	free(out->modes);

	wl_signal_emit(&backend->signals.output_rem, out);

	return false;
}

static void page_flip_handler(int fd, unsigned seq, unsigned tv_sec, unsigned tv_usec,
	void *user) {

	struct wlr_drm_output *out = user;
	struct wlr_drm_backend *backend = wl_container_of(out->renderer, backend, renderer);

	out->pageflip_pending = true;
	if (!out->cleanup) {
		wl_signal_emit(&backend->signals.output_render, out);
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

static void restore_output(struct wlr_drm_output *out, int fd) {
	drmModeCrtc *crtc = out->old_crtc;
	if (!crtc) {
		return;
	}

	// Wait for exising page flips to finish

	out->cleanup = true;
	while (out->pageflip_pending) {
		wlr_drm_event(fd, 0, NULL);
	}

	drmModeSetCrtc(fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y,
		&out->connector, 1, &crtc->mode);
	drmModeFreeCrtc(crtc);
}

void wlr_drm_output_cleanup(struct wlr_drm_output *out, bool restore) {
	if (!out) {
		return;
	}

	struct wlr_drm_renderer *renderer = out->renderer;

	switch (out->state) {
	case DRM_OUTPUT_CONNECTED:
		eglDestroySurface(renderer->egl.display, out->egl);
		gbm_surface_destroy(out->gbm);

		out->egl = EGL_NO_SURFACE;
		out->gbm = NULL;
		/* Fallthrough */

	case DRM_OUTPUT_NEEDS_MODESET:
		free(out->modes);
		out->num_modes = 0;
		out->modes = NULL;
		out->active_mode = NULL;
		out->width = 0;
		out->height = 0;

		if (restore) {
			restore_output(out, renderer->fd);
		}

		out->state = DRM_MODE_DISCONNECTED;
		break;

	case DRM_OUTPUT_DISCONNECTED:
		break;
	}
}
