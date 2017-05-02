#ifndef UDEV_H
#define UDEV_H

#include <libudev.h>

#include "backend/drm/session.h"

struct wlr_udev {
	struct udev *udev;
	struct udev_monitor *mon;
	char *drm_path;
};

struct wlr_drm_backend;
bool wlr_udev_init(struct wlr_drm_backend *backend);
void wlr_udev_free(struct wlr_udev *udev);
int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session);

void wlr_udev_event(struct wlr_drm_backend *backend);

#endif
