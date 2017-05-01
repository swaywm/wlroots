#ifndef UDEV_H
#define UDEV_H

#include <libudev.h>

#include "backend/drm/session.h"

struct wlr_udev {
	struct udev *udev;
	struct udev_monitor *mon;
	int mon_fd;
	char *drm_path;
};

bool wlr_udev_init(struct wlr_udev *udev);
void wlr_udev_free(struct wlr_udev *udev);
int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session);

struct wlr_drm_backend;
void wlr_udev_event(struct wlr_drm_backend *backend);

#endif
