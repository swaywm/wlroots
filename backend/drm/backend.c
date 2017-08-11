#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <wayland-server.h>
#include <xf86drm.h>
#include <sys/stat.h>
#include <wlr/backend/session.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include <wlr/egl.h>
#include "backend/udev.h"
#include "backend/drm.h"

static bool wlr_drm_backend_init(struct wlr_backend_state *drm) {
	wlr_drm_scan_connectors(drm);
	return true;
}

static void wlr_drm_backend_destroy(struct wlr_backend_state *drm) {
	if (!drm) {
		return;
	}

	for (size_t i = 0; drm->outputs && i < drm->outputs->length; ++i) {
		struct wlr_output_state *output = drm->outputs->items[i];
		wlr_output_destroy(output->base);
	}

	wlr_udev_signal_remove(drm->udev, &drm->drm_invalidated);
	wlr_drm_renderer_free(&drm->renderer);
	wlr_drm_resources_free(drm);
	wlr_session_close_file(drm->session, drm->fd);
	wl_event_source_remove(drm->drm_event);
	free(drm);
}

static struct wlr_egl *wlr_drm_backend_get_egl(struct wlr_backend_state *drm) {
	return &drm->renderer.egl;
}

static struct wlr_backend_impl backend_impl = {
	.init = wlr_drm_backend_init,
	.destroy = wlr_drm_backend_destroy,
	.get_egl = wlr_drm_backend_get_egl
};

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_backend_state *drm = wl_container_of(listener, drm, session_signal);
	struct wlr_session *session = data;

	if (session->active) {
		wlr_log(L_INFO, "DRM fd resumed");

		for (size_t i = 0; i < drm->outputs->length; ++i) {
			struct wlr_output_state *output = drm->outputs->items[i];
			wlr_drm_output_start_renderer(output);

			if (!output->crtc) {
				continue;
			}

			struct wlr_drm_plane *plane = output->crtc->cursor;

			drm->iface->crtc_set_cursor(drm, output->crtc,
				plane ? plane->cursor_bo : NULL);
		}
	} else {
		wlr_log(L_INFO, "DRM fd paused");
	}
}

static void drm_invalidated(struct wl_listener *listener, void *data) {
	struct wlr_backend_state *drm = wl_container_of(listener, drm, drm_invalidated);
	struct wlr_udev *udev = data;

	(void)udev;

	char *name = drmGetDeviceNameFromFd2(drm->fd);
	wlr_log(L_DEBUG, "%s invalidated", name);
	free(name);

	wlr_drm_scan_connectors(drm);
}

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
		struct wlr_session *session, struct wlr_udev *udev, int gpu_fd) {
	assert(display && session && gpu_fd >= 0);

	char *name = drmGetDeviceNameFromFd2(gpu_fd);
	drmVersion *version = drmGetVersion(gpu_fd);
	wlr_log(L_INFO, "Initalizing DRM backend for %s (%s)", name, version->name);
	free(name);
	drmFreeVersion(version);

	struct wlr_backend_state *drm = calloc(1, sizeof(*drm));
	if (!drm) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	struct wlr_backend *backend = wlr_backend_create(&backend_impl, drm);
	if (!backend) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	drm->base = backend;
	drm->session = session;
	drm->udev = udev;
	drm->outputs = list_create();
	if (!drm->outputs) {
		wlr_log(L_ERROR, "Failed to allocate list");
		goto error_backend;
	}

	drm->fd = gpu_fd;

	struct stat st;
	if (fstat(drm->fd, &st) < 0) {
		wlr_log_errno(L_ERROR, "Stat failed");
	}
	drm->dev = st.st_rdev;

	drm->drm_invalidated.notify = drm_invalidated;
	wlr_udev_signal_add(udev, drm->dev, &drm->drm_invalidated);

	drm->display = display;
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	drm->drm_event = wl_event_loop_add_fd(event_loop, drm->fd,
		WL_EVENT_READABLE, wlr_drm_event, NULL);
	if (!drm->drm_event) {
		wlr_log(L_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	drm->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &drm->session_signal);

	if (!wlr_drm_check_features(drm)) {
		goto error_event;
	}

	if (!wlr_drm_resources_init(drm)) {
		goto error_event;
	}

	if (!wlr_drm_renderer_init(&drm->renderer, drm->fd)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	if (!wlr_egl_bind_display(&drm->renderer.egl, display)) {
		wlr_log(L_INFO, "Failed to bind egl/wl display: %s", egl_error());
	}

	return backend;

error_event:
	wl_event_source_remove(drm->drm_event);
error_fd:
	wlr_session_close_file(drm->session, drm->fd);
	list_free(drm->outputs);
error_backend:
	free(drm);
	free(backend);
	return NULL;
}
