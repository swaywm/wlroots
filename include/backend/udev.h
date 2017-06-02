#ifndef _WLR_INTERNAL_UDEV_H
#define _WLR_INTERNAL_UDEV_H

#include <libudev.h>
#include <wlr/session.h>
#include <wayland-server.h>
#include <wlr/backend/udev.h>

struct wlr_udev {
	struct udev *udev;
	struct udev_monitor *mon;
	char *drm_path;
	struct wl_event_source *event;
	struct wl_signal invalidate_drm;
};

int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session);

#endif
