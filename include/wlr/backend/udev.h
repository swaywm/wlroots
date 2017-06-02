#ifndef _WLR_BACKEND_UDEV_H
#define _WLR_BACKEND_UDEV_H

struct wlr_udev;

struct wlr_udev *wlr_udev_create(struct wl_display *display);
void wlr_udev_destroy(struct wlr_udev *udev);

#endif
