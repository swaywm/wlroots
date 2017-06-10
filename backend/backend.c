#include <wayland-server.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libinput.h>
#include <wlr/session.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/libinput.h>
#include "backend/libinput.h"
#include "backend/udev.h"
#include "common/log.h"

struct wlr_backend *wlr_backend_create(const struct wlr_backend_impl *impl,
		struct wlr_backend_state *state) {
	struct wlr_backend *backend = calloc(1, sizeof(struct wlr_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	backend->state = state;
	backend->impl = impl;
	wl_signal_init(&backend->events.input_add);
	wl_signal_init(&backend->events.input_remove);
	wl_signal_init(&backend->events.output_add);
	wl_signal_init(&backend->events.output_remove);
	return backend;
}

bool wlr_backend_init(struct wlr_backend *backend) {
	return backend->impl->init(backend->state);
}

void wlr_backend_destroy(struct wlr_backend *backend) {
	backend->impl->destroy(backend->state);
	free(backend);
}

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display,
		struct wlr_session *session) {
	// TODO: Choose the most appropriate backend for the situation
	// Attempt DRM+libinput
	struct wlr_udev *udev;
	if (!(udev = wlr_udev_create(display))) {
		wlr_log(L_ERROR, "Failed to start udev");
		goto error;
	}
	struct wlr_backend *wlr;
	wlr = wlr_libinput_backend_create(display, session, udev);
	return wlr;
	int gpu = wlr_udev_find_gpu(udev, session);
	if (gpu == -1) {
		wlr_log(L_ERROR, "Failed to open DRM device");
		goto error_udev;
	}
	wlr = wlr_drm_backend_create(display, session, udev, gpu);
	if (!wlr) {
		goto error_gpu;
	}
	return wlr;
error_gpu:
	close(gpu);
error_udev:
	wlr_udev_destroy(udev);
error:
	return NULL;
}

struct libinput_device *wlr_libinput_get_device_handle(struct wlr_input_device *dev) {
	return dev->state->handle;
}
