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
#include <wlr/backend/wayland.h>
#include <wlr/backend/multi.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "backend/udev.h"

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

static struct wlr_backend *attempt_wl_backend(struct wl_display *display) {
	struct wlr_backend *backend = wlr_wl_backend_create(display);
	if (backend) {
		int outputs = 1;
		const char *_outputs = getenv("WLR_WL_OUTPUTS");
		if (_outputs) {
			char *end;
			outputs = (int)strtol(_outputs, &end, 10);
			if (*end) {
				wlr_log(L_ERROR, "WLR_WL_OUTPUTS specified with invalid integer, ignoring");
				outputs = 1;
			} else if (outputs < 0) {
				wlr_log(L_ERROR, "WLR_WL_OUTPUTS specified with negative outputs, ignoring");
				outputs = 1;
			}
		}
		while (outputs--) {
			wlr_wl_output_create(backend);
		}
	}
	return backend;
}

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display,
		struct wlr_session *session) {
	struct wlr_backend *backend;
	if (getenv("WAYLAND_DISPLAY") || getenv("_WAYLAND_DISPLAY")) {
		backend = attempt_wl_backend(display);
		if (backend) {
			return backend;
		}
	}
	// Attempt DRM+libinput
	struct wlr_udev *udev;
	if (!(udev = wlr_udev_create(display))) {
		wlr_log(L_ERROR, "Failed to start udev");
		goto error;
	}
	int gpu = wlr_udev_find_gpu(udev, session);
	if (gpu == -1) {
		wlr_log(L_ERROR, "Failed to open DRM device");
		goto error_udev;
	}
	backend = wlr_multi_backend_create();
	if (!backend) {
		goto error_gpu;
	}
	struct wlr_backend *libinput =
		wlr_libinput_backend_create(display, session, udev);
	if (!libinput) {
		goto error_multi;
	}
	struct wlr_backend *drm =
		wlr_drm_backend_create(display, session, udev, gpu);
	if (!drm) {
		goto error_libinput;
	}
	wlr_multi_backend_add(backend, libinput);
	wlr_multi_backend_add(backend, drm);
	return backend;
error_libinput:
	wlr_backend_destroy(libinput);
error_multi:
	wlr_backend_destroy(backend);
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
