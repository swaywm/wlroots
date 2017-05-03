#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <wayland-server.h>

#include <wlr/session.h>
#include <wlr/common/list.h>

#include "backend/drm/backend.h"
#include "backend/drm/drm.h"
#include "backend/drm/udev.h"
#include "common/log.h"

struct wlr_drm_backend *wlr_drm_backend_init(struct wl_display *display,
	struct wlr_session *session, struct wl_listener *add, struct wl_listener *rem,
	struct wl_listener *render) {

	struct wlr_drm_backend *backend = calloc(1, sizeof *backend);
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	backend->session = session;

	backend->outputs = list_create();
	if (!backend->outputs) {
		wlr_log(L_ERROR, "Failed to allocate list");
		goto error_backend;
	}

	if (!wlr_udev_init(display, &backend->udev)) {
		wlr_log(L_ERROR, "Failed to start udev");
		goto error_list;
	}

	backend->fd = wlr_udev_find_gpu(&backend->udev, backend->session);
	if (backend->fd == -1) {
		wlr_log(L_ERROR, "Failed to open DRM device");
		goto error_udev;
	}

	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	backend->drm_event = wl_event_loop_add_fd(event_loop, backend->fd,
		WL_EVENT_READABLE, wlr_drm_event, NULL);
	if (!backend->drm_event) {
		wlr_log(L_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	if (!wlr_drm_renderer_init(&backend->renderer, backend->fd)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	wl_signal_init(&backend->signals.output_add);
	wl_signal_init(&backend->signals.output_rem);
	wl_signal_init(&backend->signals.output_render);

	if (add) {
		wl_signal_add(&backend->signals.output_add, add);
	}
	if (rem) {
		wl_signal_add(&backend->signals.output_rem, rem);
	}
	if (render) {
		wl_signal_add(&backend->signals.output_render, render);
	}

	wlr_drm_scan_connectors(backend);

	return backend;

error_event:
	wl_event_source_remove(backend->drm_event);
error_fd:
	wlr_session_close_file(backend->session, backend->fd);
error_udev:
	wlr_udev_free(&backend->udev);
error_list:
	list_free(backend->outputs);
error_backend:
	free(backend);
	return NULL;
}

static void free_output(void *item)
{
	struct wlr_drm_output *out = item;
	wlr_drm_output_cleanup(out, true);
	free(out);
}

void wlr_drm_backend_free(struct wlr_drm_backend *backend)
{
	if (!backend)
		return;

	list_foreach(backend->outputs, free_output);

	wlr_drm_renderer_free(&backend->renderer);
	wlr_udev_free(&backend->udev);
	wlr_session_close_file(backend->session, backend->fd);
	wlr_session_finish(backend->session);

	wl_event_source_remove(backend->drm_event);

	list_free(backend->outputs);
	free(backend);
}
