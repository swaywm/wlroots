#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"
#include "util/signal.h"
#include "xdg-shell-unstable-v6-client-protocol.h"

struct wlr_wl_backend *get_wl_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_wl(wlr_backend));
	return (struct wlr_wl_backend *)wlr_backend;
}

static int dispatch_events(int fd, uint32_t mask, void *data) {
	struct wlr_wl_backend *backend = data;
	int count = 0;

	if ((mask & WL_EVENT_HANGUP) || (mask & WL_EVENT_ERROR)) {
		wl_display_terminate(backend->local_display);
		return 0;
	}

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
static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_wl_backend *backend = get_wl_backend_from_backend(wlr_backend);
	wlr_log(WLR_INFO, "Initializating wayland backend");

	poll_wl_registry(backend);
	if (!backend->compositor || !backend->shell) {
		wlr_log_errno(WLR_ERROR, "Could not obtain retrieve required globals");
		return false;
	}

	backend->started = true;

	for (size_t i = 0; i < backend->requested_outputs; ++i) {
		wlr_wl_output_create(&backend->backend);
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(backend->local_display);
	int fd = wl_display_get_fd(backend->remote_display);
	int events = WL_EVENT_READABLE | WL_EVENT_ERROR | WL_EVENT_HANGUP;
	backend->remote_display_src = wl_event_loop_add_fd(loop, fd, events,
		dispatch_events, backend);
	wl_event_source_check(backend->remote_display_src);

	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_wl_backend *backend = get_wl_backend_from_backend(wlr_backend);
	if (backend == NULL) {
		return;
	}

	struct wlr_wl_output *output, *tmp_output;
	wl_list_for_each_safe(output, tmp_output, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	struct wlr_input_device *input_device, *tmp_input_device;
	wl_list_for_each_safe(input_device, tmp_input_device, &backend->devices, link) {
		wlr_input_device_destroy(input_device);
	}

	wlr_signal_emit_safe(&wlr_backend->events.destroy, wlr_backend);

	wl_list_remove(&backend->local_display_destroy.link);

	free(backend->seat_name);

	if (backend->remote_display_src) {
		wl_event_source_remove(backend->remote_display_src);
	}
	wlr_renderer_destroy(backend->renderer);
	if (backend->pointer) {
		wl_pointer_destroy(backend->pointer);
	}
	if (backend->seat) {
		wl_seat_destroy(backend->seat);
	}
	if (backend->shm) {
		wl_shm_destroy(backend->shm);
	}
	if (backend->shell) {
		zxdg_shell_v6_destroy(backend->shell);
	}
	if (backend->compositor) {
		wl_compositor_destroy(backend->compositor);
	}
	if (backend->registry) {
		wl_registry_destroy(backend->registry);
	}
	if (backend->remote_display) {
		wl_display_disconnect(backend->remote_display);
	}
	free(backend);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *wlr_backend) {
	struct wlr_wl_backend *backend = get_wl_backend_from_backend(wlr_backend);
	return backend->renderer;
}

static struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

bool wlr_backend_is_wl(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_wl_backend *backend =
		wl_container_of(listener, backend, local_display_destroy);
	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_wl_backend_create(struct wl_display *display,
		const char *remote, wlr_renderer_create_func_t create_renderer_func) {
	wlr_log(WLR_INFO, "Creating wayland backend");

	struct wlr_wl_backend *backend = calloc(1, sizeof(struct wlr_wl_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}
	wlr_backend_init(&backend->backend, &backend_impl);

	wl_list_init(&backend->devices);
	wl_list_init(&backend->outputs);

	backend->local_display = display;

	backend->remote_display = wl_display_connect(remote);
	if (!backend->remote_display) {
		wlr_log_errno(WLR_ERROR, "Could not connect to remote display");
		goto error_connect;
	}

	backend->registry = wl_display_get_registry(backend->remote_display);
	if (backend->registry == NULL) {
		wlr_log_errno(WLR_ERROR, "Could not obtain reference to remote registry");
		goto error_registry;
	}

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	backend->renderer = create_renderer_func(&backend->backend);
	if (backend->renderer == NULL) {
		wlr_log(WLR_ERROR, "Could not create renderer");
		goto error_renderer;
	}

	backend->local_display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->local_display_destroy);

	return &backend->backend;

error_renderer:
	wl_registry_destroy(backend->registry);
error_registry:
	wl_display_disconnect(backend->remote_display);
error_connect:
	free(backend);
	return NULL;
}
