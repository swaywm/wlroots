#ifndef UDEV_H
#define UDEV_H

#include <libudev.h>

#include <wlr/session.h>
#include <wayland-server.h>

struct wlr_udev {
	struct udev *udev;
	struct udev_monitor *mon;
	char *drm_path;

	struct wl_event_source *event;
};

struct wlr_drm_backend;
bool wlr_udev_init(struct wl_display *display, struct wlr_udev *udev);
void wlr_udev_free(struct wlr_udev *udev);
int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session);

void wlr_udev_event(struct wlr_drm_backend *backend);

#endif
