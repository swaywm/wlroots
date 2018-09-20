#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <EGL/egl.h>
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

	finish_drm_renderer(&drm->renderer);
	finish_drm_resources(drm);
	wlr_session_close_file(drm->session, drm->fd);
	wl_event_source_remove(drm->drm_event);
	free(drm);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);

	if (drm->parent) {
		return drm->parent->renderer.wlr_rend;
	} else {
		return drm->renderer.wlr_rend;
	}
}

static clockid_t backend_get_presentation_clock(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	return drm->clock;
}

static bool backend_egl_params(struct wlr_backend *wlr_backend,
		EGLenum *platform, void **remote_display, const EGLint **config_attribs,
		EGLint *visualid) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(wlr_backend);
	assert(drm->renderer.gbm);

	*config_attribs = NULL;
	*platform = EGL_PLATFORM_GBM_MESA;
	*remote_display = drm->renderer.gbm;
	*visualid = GBM_FORMAT_ARGB8888;
	return true;
}

static struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
	.get_presentation_clock = backend_get_presentation_clock,
	.egl_params = backend_egl_params,
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

		struct wlr_drm_connector *conn;
		wl_list_for_each(conn, &drm->outputs, link){
			if (conn->output.enabled) {
				wlr_output_set_mode(&conn->output, conn->output.current_mode);
			} else {
				enable_drm_connector(&conn->output, false);
			}

			if (!conn->crtc) {
				continue;
			}

			struct wlr_drm_plane *plane = conn->crtc->cursor;
			drm->iface->crtc_set_cursor(drm, conn->crtc,
				(plane && plane->cursor_enabled) ? plane->cursor_bo : NULL);
			drm->iface->crtc_move_cursor(drm, conn->crtc, conn->cursor_x,
				conn->cursor_y);

			if (conn->crtc->gamma_table != NULL) {
				size_t size = conn->crtc->gamma_table_size;
				uint16_t *r = conn->crtc->gamma_table;
				uint16_t *g = conn->crtc->gamma_table + size;
				uint16_t *b = conn->crtc->gamma_table + 2 * size;
				drm->iface->crtc_set_gamma(drm, conn->crtc, size, r, g, b);
			} else {
				set_drm_connector_gamma(&conn->output, 0, NULL, NULL, NULL);
			}
		}
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
	wlr_backend_init(&drm->backend, &backend_impl);

	drm->session = session;
	wl_list_init(&drm->outputs);

	drm->fd = gpu_fd;
	if (parent != NULL) {
		drm->parent = get_drm_backend_from_backend(parent);
	}

	drm->drm_invalidated.notify = drm_invalidated;
	wlr_session_signal_add(session, gpu_fd, &drm->drm_invalidated);

	drm->display = display;
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	drm->drm_event = wl_event_loop_add_fd(event_loop, drm->fd,
		WL_EVENT_READABLE, handle_drm_event, NULL);
	if (!drm->drm_event) {
		wlr_log(WLR_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	drm->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &drm->session_signal);

	if (!check_drm_features(drm)) {
		goto error_event;
	}

	if (!init_drm_resources(drm)) {
		goto error_event;
	}

	if (!init_drm_renderer(drm, &drm->renderer, create_renderer_func)) {
		wlr_log(WLR_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	drm->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &drm->display_destroy);

	return &drm->backend;

error_event:
	wl_list_remove(&drm->session_signal.link);
	wl_event_source_remove(drm->drm_event);
error_fd:
	wlr_session_close_file(drm->session, drm->fd);
	free(drm);
	return NULL;
}
