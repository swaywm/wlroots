#include <assert.h>
#include <errno.h>
#include <libinput.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/util/log.h>
#include "util/defs.h"

WLR_API
void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl) {
	assert(backend);
	backend->impl = impl;
	wl_signal_init(&backend->events.destroy);
	wl_signal_init(&backend->events.new_input);
	wl_signal_init(&backend->events.new_output);
}

WLR_API
bool wlr_backend_start(struct wlr_backend *backend) {
	if (backend->impl->start) {
		return backend->impl->start(backend);
	}
	return true;
}

WLR_API
void wlr_backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	if (backend->impl && backend->impl->destroy) {
		backend->impl->destroy(backend);
	} else {
		free(backend);
	}
}

WLR_API
struct wlr_egl *wlr_backend_get_egl(struct wlr_backend *backend) {
	if (backend->impl->get_egl) {
		return backend->impl->get_egl(backend);
	}
	return NULL;
}

WLR_API
struct wlr_renderer *wlr_backend_get_renderer(struct wlr_backend *backend) {
	if (backend->impl->get_renderer) {
		return backend->impl->get_renderer(backend);
	}
	return NULL;
}

static struct wlr_backend *attempt_wl_backend(struct wl_display *display) {
	struct wlr_backend *backend = wlr_wl_backend_create(display, NULL);
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

WLR_API
struct wlr_backend *wlr_backend_autocreate(struct wl_display *display) {
	struct wlr_backend *backend = wlr_multi_backend_create(display);
	if (!backend) {
		wlr_log(L_ERROR, "could not allocate multibackend");
		return NULL;
	}

	if (getenv("WAYLAND_DISPLAY") || getenv("_WAYLAND_DISPLAY")) {
		struct wlr_backend *wl_backend = attempt_wl_backend(display);
		if (wl_backend) {
			wlr_multi_backend_add(backend, wl_backend);
			return backend;
		}
	}

	const char *x11_display = getenv("DISPLAY");
	if (x11_display) {
		struct wlr_backend *x11_backend =
			wlr_x11_backend_create(display, x11_display);
		wlr_multi_backend_add(backend, x11_backend);
		return backend;
	}

	// Attempt DRM+libinput
	struct wlr_session *session = wlr_session_create(display);
	if (!session) {
		wlr_log(L_ERROR, "Failed to start a DRM session");
		wlr_backend_destroy(backend);
		return NULL;
	}

	struct wlr_backend *libinput = wlr_libinput_backend_create(display, session);
	if (libinput) {
		wlr_multi_backend_add(backend, libinput);
	} else {
		wlr_log(L_ERROR, "Failed to start libinput backend");
		wlr_backend_destroy(backend);
		wlr_session_destroy(session);
		return NULL;
	}

	int gpus[8];
	size_t num_gpus = wlr_session_find_gpus(session, 8, gpus);
	struct wlr_backend *primary_drm = NULL;
	wlr_log(L_INFO, "Found %zu GPUs", num_gpus);

	for (size_t i = 0; i < num_gpus; ++i) {
		struct wlr_backend *drm = wlr_drm_backend_create(display, session,
			gpus[i], primary_drm);
		if (!drm) {
			wlr_log(L_ERROR, "Failed to open DRM device");
			continue;
		}

		if (!primary_drm) {
			primary_drm = drm;
		}

		wlr_multi_backend_add(backend, drm);
	}

	if (!primary_drm) {
		wlr_log(L_ERROR, "Failed to open any DRM device");
		wlr_backend_destroy(libinput);
		wlr_session_destroy(session);
		wlr_backend_destroy(backend);
		return NULL;
	}

	return backend;
}
