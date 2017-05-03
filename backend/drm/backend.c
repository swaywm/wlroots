#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <wlr/session.h>
#include <wlr/common/list.h>

#include "backend/drm/backend.h"
#include "backend/drm/drm.h"
#include "backend/drm/udev.h"
#include "common/log.h"

struct wlr_drm_backend *wlr_drm_backend_init(struct wlr_session *session,
	struct wl_listener *add, struct wl_listener *rem, struct wl_listener *render)
{
	struct wlr_drm_backend *backend = calloc(1, sizeof *backend);
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	backend->session = session;

	backend->displays = list_create();
	if (!backend->displays) {
		wlr_log(L_ERROR, "Failed to allocate list");
		goto error_backend;
	}

	backend->event_loop = wl_event_loop_create();
	if (!backend->event_loop) {
		wlr_log(L_ERROR, "Failed to create event loop");
		goto error_list;
	}

	if (!wlr_udev_init(backend)) {
		wlr_log(L_ERROR, "Failed to start udev");
		goto error_loop;
	}

	backend->fd = wlr_udev_find_gpu(&backend->udev, backend->session);
	if (backend->fd == -1) {
		wlr_log(L_ERROR, "Failed to open DRM device");
		goto error_udev;
	}

	if (!wlr_drm_renderer_init(&backend->renderer, backend, backend->fd)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_fd;
	}

	wl_signal_init(&backend->signals.display_add);
	wl_signal_init(&backend->signals.display_rem);
	wl_signal_init(&backend->signals.display_render);

	if (add)
		wl_signal_add(&backend->signals.display_add, add);
	if (rem)
		wl_signal_add(&backend->signals.display_rem, rem);
	if (render)
		wl_signal_add(&backend->signals.display_render, render);

	wlr_drm_scan_connectors(backend);

	return backend;

error_fd:
	wlr_session_close_file(backend->session, backend->fd);
error_udev:
	wlr_udev_free(&backend->udev);
error_loop:
	wl_event_loop_destroy(backend->event_loop);
error_list:
	list_free(backend->displays);
error_backend:
	free(backend);
	return NULL;
}

static void free_display(void *item)
{
	struct wlr_drm_display *disp = item;
	wlr_drm_display_free(disp, true);
	free(disp);
}

void wlr_drm_backend_free(struct wlr_drm_backend *backend)
{
	if (!backend)
		return;

	list_foreach(backend->displays, free_display);

	wlr_drm_renderer_free(&backend->renderer);
	wlr_udev_free(&backend->udev);
	wlr_session_close_file(backend->session, backend->fd);
	wlr_session_finish(backend->session);

	wl_event_source_remove(backend->event_src.drm);
	wl_event_source_remove(backend->event_src.udev);
	wl_event_loop_destroy(backend->event_loop);

	list_free(backend->displays);
	free(backend);
}

struct wl_event_loop *wlr_drm_backend_get_event_loop(struct wlr_drm_backend *backend)
{
	return backend->event_loop;
}
