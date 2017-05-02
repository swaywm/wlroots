#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "backend/drm/backend.h"
#include "backend/drm/drm.h"
#include "backend/drm/event.h"
#include "backend/drm/session.h"
#include "backend/drm/udev.h"
#include "common/log.h"

struct wlr_drm_backend *wlr_drm_backend_init(void)
{
	struct wlr_drm_backend *backend = calloc(1, sizeof *backend);
	if (!backend) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	if (!wlr_session_start(&backend->session)) {
		wlr_log(L_ERROR, "Failed to start session");
		goto error;
	}

	if (!wlr_udev_init(&backend->udev)) {
		wlr_log(L_ERROR, "Failed to start udev");
		goto error_session;
	}

	backend->fd = wlr_udev_find_gpu(&backend->udev, &backend->session);
	if (backend->fd == -1) {
		wlr_log(L_ERROR, "Failed to open DRM device");
		goto error_udev;
	}

	if (!wlr_drm_renderer_init(&backend->renderer, backend, backend->fd)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_fd;
	}

	wlr_drm_scan_connectors(backend);

	return backend;

error_fd:
	wlr_session_release_device(&backend->session, backend->fd);
error_udev:
	wlr_udev_free(&backend->udev);
error_session:
	wlr_session_end(&backend->session);
error:
	free(backend);
	return NULL;
}

void wlr_drm_backend_free(struct wlr_drm_backend *backend)
{
	if (!backend)
		return;

	for (size_t i = 0; i < backend->display_len; ++i) {
		wlr_drm_display_free(&backend->displays[i], true);
	}

	wlr_drm_renderer_free(&backend->renderer);
	wlr_udev_free(&backend->udev);
	wlr_session_release_device(&backend->session, backend->fd);
	wlr_session_end(&backend->session);

	free(backend->events);
	free(backend->displays);
	free(backend);
}

