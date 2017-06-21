#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <wayland-server.h>
#include <xf86drm.h>
#include <sys/stat.h>
#include <wlr/session.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include "backend/udev.h"
#include "backend/drm.h"

static bool wlr_drm_backend_init(struct wlr_backend_state *state) {
	wlr_drm_scan_connectors(state);
	return true;
}

static void wlr_drm_backend_destroy(struct wlr_backend_state *state) {
	if (!state) {
		return;
	}
	for (size_t i = 0; state->outputs && i < state->outputs->length; ++i) {
		struct wlr_output_state *output = state->outputs->items[i];
		wlr_output_destroy(output->wlr_output);
	}
	wlr_udev_signal_remove(state->udev, &state->drm_invalidated);
	wlr_drm_renderer_free(&state->renderer);
	wlr_session_close_file(state->session, state->fd);
	wl_event_source_remove(state->drm_event);
	free(state);
}

static struct wlr_backend_impl backend_impl = {
	.init = wlr_drm_backend_init,
	.destroy = wlr_drm_backend_destroy
};

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_backend_state *drm = wl_container_of(listener, drm, session_signal);
	struct wlr_session *session = data;

	if (session->active) {
		wlr_log(L_INFO, "DRM fd resumed");

		for (size_t i = 0; i < drm->outputs->length; ++i) {
			struct wlr_output_state *output = drm->outputs->items[i];
			wlr_drm_output_start_renderer(output);
		}
	} else {
		wlr_log(L_INFO, "DRM fd paused");

		for (size_t i = 0; i < drm->outputs->length; ++i) {
			struct wlr_output_state *output = drm->outputs->items[i];
			wlr_drm_output_pause_renderer(output);
		}
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
	assert(display && session && gpu_fd > 0);

	char *name = drmGetDeviceNameFromFd2(gpu_fd);
	drmVersion *version = drmGetVersion(gpu_fd);

	wlr_log(L_INFO, "Initalizing DRM backend for %s (%s)", name, version->name);

	free(name);
	drmFreeVersion(version);

	struct wlr_backend_state *state = calloc(1, sizeof(struct wlr_backend_state));
	if (!state) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	struct wlr_backend *backend = wlr_backend_create(&backend_impl, state);
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	state->backend = backend;
	state->session = session;
	state->udev = udev;
	state->outputs = list_create();
	if (!state->outputs) {
		wlr_log(L_ERROR, "Failed to allocate list");
		goto error_backend;
	}

	state->fd = gpu_fd;

	struct stat st;
	if (fstat(state->fd, &st) < 0) {
		wlr_log(L_ERROR, "Stat failed: %s", strerror(errno));
	}
	state->dev = st.st_rdev;

	state->drm_invalidated.notify = drm_invalidated;
	wlr_udev_signal_add(udev, state->dev, &state->drm_invalidated);

	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	state->drm_event = wl_event_loop_add_fd(event_loop, state->fd,
		WL_EVENT_READABLE, wlr_drm_event, NULL);
	if (!state->drm_event) {
		wlr_log(L_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	state->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &state->session_signal);

	// TODO: what is the difference between the per-output renderer and this
	// one?
	if (!wlr_drm_renderer_init(&state->renderer, state->fd)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	return backend;

error_event:
	wl_event_source_remove(state->drm_event);
error_fd:
	wlr_session_close_file(state->session, state->fd);
	list_free(state->outputs);
error_backend:
	free(state);
	free(backend);
	return NULL;
}
