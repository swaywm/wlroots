#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <wayland-server.h>
#include <xf86drm.h>
#include <wlr/backend/session.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include <wlr/egl.h>
#include "backend/drm/drm.h"

static bool wlr_drm_backend_start(struct wlr_backend *_backend) {
	struct wlr_drm_backend *backend = (struct wlr_drm_backend *)_backend;
	wlr_drm_scan_connectors(backend);
	return true;
}

static void wlr_drm_backend_destroy(struct wlr_backend *_backend) {
	if (!_backend) {
		return;
	}
	struct wlr_drm_backend *backend = (struct wlr_drm_backend *)_backend;

	wlr_drm_restore_outputs(backend);

	for (size_t i = 0; backend->outputs && i < backend->outputs->length; ++i) {
		struct wlr_drm_output *output = backend->outputs->items[i];
		wlr_output_destroy(&output->output);
	}

	wlr_drm_renderer_finish(&backend->renderer);
	wlr_drm_resources_free(backend);
	wlr_session_close_file(backend->session, backend->fd);
	wl_event_source_remove(backend->drm_event);
	list_free(backend->outputs);
	free(backend);
}

static struct wlr_egl *wlr_drm_backend_get_egl(struct wlr_backend *_backend) {
	struct wlr_drm_backend *backend = (struct wlr_drm_backend *)_backend;
	return &backend->renderer.egl;
}

static struct wlr_backend_impl backend_impl = {
	.start = wlr_drm_backend_start,
	.destroy = wlr_drm_backend_destroy,
	.get_egl = wlr_drm_backend_get_egl
};

bool wlr_backend_is_drm(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *backend =
		wl_container_of(listener, backend, session_signal);
	struct wlr_session *session = data;

	if (session->active) {
		wlr_log(L_INFO, "DRM fd resumed");

		for (size_t i = 0; i < backend->outputs->length; ++i) {
			struct wlr_drm_output *output = backend->outputs->items[i];
			wlr_drm_output_start_renderer(output);

			if (!output->crtc) {
				continue;
			}

			struct wlr_drm_plane *plane = output->crtc->cursor;
			backend->iface->crtc_set_cursor(backend, output->crtc,
				plane ? plane->cursor_bo : NULL);
		}
	} else {
		wlr_log(L_INFO, "DRM fd paused");
	}
}

static void drm_invalidated(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *backend =
		wl_container_of(listener, backend, drm_invalidated);

	char *name = drmGetDeviceNameFromFd2(backend->fd);
	wlr_log(L_DEBUG, "%s invalidated", name);
	free(name);

	wlr_drm_scan_connectors(backend);
}

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
		struct wlr_session *session, int gpu_fd) {
	assert(display && session && gpu_fd >= 0);

	char *name = drmGetDeviceNameFromFd2(gpu_fd);
	drmVersion *version = drmGetVersion(gpu_fd);
	wlr_log(L_INFO, "Initalizing DRM backend for %s (%s)", name, version->name);
	free(name);
	drmFreeVersion(version);

	struct wlr_drm_backend *backend = calloc(1, sizeof(struct wlr_drm_backend));
	if (!backend) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);

	backend->session = session;
	backend->outputs = list_create();
	if (!backend->outputs) {
		wlr_log(L_ERROR, "Failed to allocate list");
		goto error_backend;
	}

	backend->fd = gpu_fd;

	backend->drm_invalidated.notify = drm_invalidated;
	wlr_session_signal_add(session, gpu_fd, &backend->drm_invalidated);

	backend->display = display;
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	backend->drm_event = wl_event_loop_add_fd(event_loop, backend->fd,
		WL_EVENT_READABLE, wlr_drm_event, NULL);
	if (!backend->drm_event) {
		wlr_log(L_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	backend->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &backend->session_signal);

	if (!wlr_drm_check_features(backend)) {
		goto error_event;
	}

	if (!wlr_drm_resources_init(backend)) {
		goto error_event;
	}

	if (!wlr_drm_renderer_init(&backend->renderer, backend->fd)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	if (!wlr_egl_bind_display(&backend->renderer.egl, display)) {
		wlr_log(L_INFO, "Failed to bind egl/wl display: %s", egl_error());
	}

	return &backend->backend;

error_event:
	wl_event_source_remove(backend->drm_event);
error_fd:
	wlr_session_close_file(backend->session, backend->fd);
	list_free(backend->outputs);
error_backend:
	free(backend);
	return NULL;
}
