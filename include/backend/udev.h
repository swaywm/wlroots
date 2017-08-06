#ifndef _WLR_INTERNAL_UDEV_H
#define _WLR_INTERNAL_UDEV_H

#include <sys/types.h>
#include <libudev.h>
#include <wlr/backend/session.h>
#include <wayland-server.h>
#include <wlr/backend/udev.h>

struct wlr_udev_dev {
	dev_t dev;
	struct wl_signal invalidate;

	struct wl_list link;
};

struct wlr_udev {
	struct udev *udev;
	struct udev_monitor *mon;
	struct wl_event_source *event;

	struct wl_list devices;
};

int wlr_udev_find_gpu(struct wlr_udev *udev, struct wlr_session *session);
bool wlr_udev_signal_add(struct wlr_udev *udev, dev_t dev, struct wl_listener *listener);
void wlr_udev_signal_remove(struct wlr_udev *udev, struct wl_listener *listener);

#endif
