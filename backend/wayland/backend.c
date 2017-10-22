#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-server.h>
#include <wlr/render/egl.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"
#include "xdg-shell-unstable-v6-client-protocol.h"


static int dispatch_events(int fd, uint32_t mask, void *data) {
	struct wlr_wl_backend *backend = data;
	int count = 0;
	if (mask & WL_EVENT_READABLE) {
		count = wl_display_dispatch(backend->remote_display);
	}
	if (mask == 0) {
		count = wl_display_dispatch_pending(backend->remote_display);
		wl_display_flush(backend->remote_display);
	}
	return count;
}

/*
 * Initializes the wayland backend. Opens a connection to a remote wayland
 * compositor and creates surfaces for each output, then registers globals on
 * the specified display.
 */
static bool wlr_wl_backend_start(struct wlr_backend *_backend) {
	struct wlr_wl_backend *backend = (struct wlr_wl_backend *)_backend;
	wlr_log(L_INFO, "Initializating wayland backend");

	wlr_wl_registry_poll(backend);
	if (!(backend->compositor) || (!(backend->shell))) {
		wlr_log_errno(L_ERROR, "Could not obtain retrieve required globals");
		return false;
	}

	backend->started = true;

	for (size_t i = 0; i < backend->requested_outputs; ++i) {
		wlr_wl_output_create(&backend->backend);
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(backend->local_display);
	int fd = wl_display_get_fd(backend->remote_display);
	int events = WL_EVENT_READABLE | WL_EVENT_ERROR |
		WL_EVENT_HANGUP;
	backend->remote_display_src = wl_event_loop_add_fd(loop, fd, events,
			dispatch_events, backend);
	wl_event_source_check(backend->remote_display_src);

	return true;
}

static void wlr_wl_backend_destroy(struct wlr_backend *_backend) {
	struct wlr_wl_backend *backend = (struct wlr_wl_backend *)_backend;
	if (!_backend) {
		return;
	}

	struct wlr_wl_backend_output *output, *tmp_output;
	wl_list_for_each_safe(output, tmp_output, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	struct wlr_input_device *input_device, *tmp_input_device;
	wl_list_for_each_safe(input_device, tmp_input_device, &backend->devices, link) {
		wlr_input_device_destroy(input_device);
	}

	free(backend->seat_name);

	wl_event_source_remove(backend->remote_display_src);
	wlr_egl_free(&backend->egl);
	if (backend->seat) wl_seat_destroy(backend->seat);
	if (backend->shm) wl_shm_destroy(backend->shm);
	if (backend->shell) zxdg_shell_v6_destroy(backend->shell);
	if (backend->compositor) wl_compositor_destroy(backend->compositor);
	if (backend->registry) wl_registry_destroy(backend->registry);
	if (backend->remote_display) wl_display_disconnect(backend->remote_display);
	free(backend);
}

static struct wlr_egl *wlr_wl_backend_get_egl(struct wlr_backend *_backend) {
	struct wlr_wl_backend *backend = (struct wlr_wl_backend *)_backend;
	return &backend->egl;
}

static struct wlr_backend_impl backend_impl = {
	.start = wlr_wl_backend_start,
	.destroy = wlr_wl_backend_destroy,
	.get_egl = wlr_wl_backend_get_egl
};

bool wlr_backend_is_wl(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

struct wlr_wl_backend_output *wlr_wl_output_for_surface(
		struct wlr_wl_backend *backend, struct wl_surface *surface) {
	struct wlr_wl_backend_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		if (output->surface == surface) {
			return output;
		}
	}
	return NULL;
}

struct wlr_backend *wlr_wl_backend_create(struct wl_display *display) {
	wlr_log(L_INFO, "Creating wayland backend");

	struct wlr_wl_backend *backend = calloc(1, sizeof(struct wlr_wl_backend));
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);

	wl_list_init(&backend->devices);
	wl_list_init(&backend->outputs);

	backend->local_display = display;

	backend->remote_display = wl_display_connect(NULL);
	if (!backend->remote_display) {
		wlr_log_errno(L_ERROR, "Could not connect to remote display");
		return false;
	}

	if (!(backend->registry = wl_display_get_registry(backend->remote_display))) {
		wlr_log_errno(L_ERROR, "Could not obtain reference to remote registry");
		return false;
	}

	wlr_egl_init(&backend->egl, EGL_PLATFORM_WAYLAND_EXT, WL_SHM_FORMAT_ARGB8888, backend->remote_display);
	wlr_egl_bind_display(&backend->egl, backend->local_display);

	return &backend->backend;
}
