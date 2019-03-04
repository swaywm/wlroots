#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <gbm.h>
#include <wayland-server.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>

#include "backend/drm/drm.h"
#include "util/signal.h"

struct wlr_drm_backend *get_drm_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_drm(wlr_backend));
	return (struct wlr_drm_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	scan_drm_connectors(drm);
	return true;
}

static void backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);

	restore_drm_outputs(drm);

	struct wlr_drm_connector *conn, *next;
	wl_list_for_each_safe(conn, next, &drm->outputs, link) {
		wlr_output_destroy(&conn->output);
	}

	wlr_signal_emit_safe(&backend->events.destroy, backend);

	wl_list_remove(&drm->display_destroy.link);
	wl_list_remove(&drm->session_signal.link);
	wl_list_remove(&drm->drm_invalidated.link);

	finish_drm_resources(drm);
	wlr_session_close_file(drm->session, drm->fd);
	wl_event_source_remove(drm->drm_event);
	close(drm->render_fd);
	free(drm);
}

static clockid_t backend_get_presentation_clock(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	return drm->clock;
}

static int backend_get_render_fd(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	//return drm->render_fd;
	return drm->fd;
}

static bool backend_attach_gbm(struct wlr_backend *backend, struct wlr_gbm_image *img) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	struct gbm_bo *bo = img->bo;

	uint32_t width = gbm_bo_get_width(bo);
	uint32_t height = gbm_bo_get_height(bo);
	uint32_t format = gbm_bo_get_format(bo);
	uint64_t mod = gbm_bo_get_modifier(bo);
	uint32_t handles[4] = { 0 };
	uint32_t strides[4] = { 0 };
	uint32_t offsets[4] = { 0 };
	uint64_t mods[4] = { 0 };
	int num_planes = gbm_bo_get_plane_count(bo);

	for (int i = 0; i < num_planes; ++i) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
		mods[i] = mod;
	}

	uint32_t fb_id;
	int ret;

	if (!drm->has_modifiers || mod == DRM_FORMAT_MOD_INVALID) {
		ret = drmModeAddFB2(drm->fd, width, height, format, handles,
			strides, offsets, &fb_id, 0);
	} else {
		ret = drmModeAddFB2WithModifiers(drm->fd, width, height, format,
			handles, strides, offsets, mods, &fb_id, DRM_MODE_FB_MODIFIERS);
	}

	if (ret) {
		wlr_log_errno(WLR_ERROR, "Failed to add DRM framebuffer");
		return false;
	}

	img->base.backend_priv = (void *)(uintptr_t)fb_id;
	return true;
}

static void backend_detach_gbm(struct wlr_backend *backend, struct wlr_gbm_image *img) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	uint32_t fb_id = (uint32_t)(uintptr_t)img->base.backend_priv;

	drmModeRmFB(drm->fd, fb_id);
	img->base.backend_priv = NULL;
}

static struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_presentation_clock = backend_get_presentation_clock,
	.get_render_fd = backend_get_render_fd,
	.attach_gbm = backend_attach_gbm,
	.detach_gbm = backend_detach_gbm,
};

bool wlr_backend_is_drm(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, session_signal);
	struct wlr_session *session = data;

	if (session->active) {
		wlr_log(WLR_INFO, "DRM fd resumed");
		scan_drm_connectors(drm);
#if 0

		struct wlr_drm_connector *conn;
		wl_list_for_each(conn, &drm->outputs, link) {
			if (conn->output.enabled) {
				drm_connector_set_mode(&conn->output,
						conn->output.current_mode);
			} else {
				enable_drm_connector(&conn->output, false);
			}

			if (!conn->crtc) {
				continue;
			}

			struct wlr_drm_plane *plane = conn->crtc->cursor;
			drm->iface->crtc_set_cursor(drm, conn->crtc,
				(plane && plane->cursor_enabled) ? plane->img : NULL);
			drm->iface->crtc_move_cursor(drm, conn->crtc, conn->cursor_x,
				conn->cursor_y);
		}
#endif
	} else {
		wlr_log(WLR_INFO, "DRM fd paused");
	}
}

static void drm_invalidated(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, drm_invalidated);

	char *name = drmGetDeviceNameFromFd2(drm->fd);
	wlr_log(WLR_DEBUG, "%s invalidated", name);
	free(name);

	scan_drm_connectors(drm);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, display_destroy);
	backend_destroy(&drm->backend);
}

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
		struct wlr_session *session, int gpu_fd, struct wlr_backend *parent,
		wlr_renderer_create_func_t create_renderer_func) {
	assert(display && session && gpu_fd >= 0);
	assert(!parent || wlr_backend_is_drm(parent));

	char *name = drmGetDeviceNameFromFd2(gpu_fd);
	drmVersion *version = drmGetVersion(gpu_fd);
	wlr_log(WLR_INFO, "Initializing DRM backend for %s (%s)", name, version->name);
	free(name);
	drmFreeVersion(version);

	struct wlr_drm_backend *drm = calloc(1, sizeof(struct wlr_drm_backend));
	if (!drm) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	drm->session = session;
	wl_list_init(&drm->outputs);

	drm->fd = gpu_fd;
	if (parent != NULL) {
		drm->parent = get_drm_backend_from_backend(parent);
	}

	char *render_name = drmGetRenderDeviceNameFromFd(gpu_fd);
	if (!render_name) {
		wlr_log_errno(WLR_ERROR, "Failed to get render device name");
		goto error_fd;
	}

	drm->render_fd = open(render_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (drm->render_fd < 0) {
		wlr_log_errno(WLR_ERROR, "Failed to open render node %s", render_name);
	}
	free(render_name);

	uint64_t val;
	drm->has_modifiers =
		drmGetCap(drm->fd, DRM_CAP_ADDFB2_MODIFIERS, &val) == 0 && val;

	drm->drm_invalidated.notify = drm_invalidated;
	wlr_session_signal_add(session, gpu_fd, &drm->drm_invalidated);

	drm->display = display;
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	drm->drm_event = wl_event_loop_add_fd(event_loop, drm->fd,
		WL_EVENT_READABLE, handle_drm_event, NULL);
	if (!drm->drm_event) {
		wlr_log(WLR_ERROR, "Failed to create DRM event source");
		goto error_render;
	}

	drm->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &drm->session_signal);

	if (!check_drm_features(drm)) {
		goto error_event;
	}

	if (!init_drm_resources(drm)) {
		goto error_event;
	}

	wlr_backend_init(&drm->backend, &backend_impl, create_renderer_func,
		DRM_FORMAT_ARGB8888);

	drm->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &drm->display_destroy);

	return &drm->backend;

error_event:
	wl_list_remove(&drm->session_signal.link);
	wl_event_source_remove(drm->drm_event);
error_render:
	close(drm->render_fd);
error_fd:
	wlr_session_close_file(drm->session, drm->fd);
	free(drm);
	return NULL;
}
