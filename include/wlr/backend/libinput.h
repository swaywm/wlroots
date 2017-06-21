#ifndef WLR_BACKEND_LIBINPUT_H
#define WLR_BACKEND_LIBINPUT_H

#include <libinput.h>
#include <wayland-server.h>
#include <wlr/session.h>
#include <wlr/backend.h>
#include <wlr/backend/udev.h>
#include <wlr/types/wlr_input_device.h>

struct wlr_backend *wlr_libinput_backend_create(struct wl_display *display,
		struct wlr_session *session, struct wlr_udev *udev);
struct libinput_device *wlr_libinput_get_device_handle(struct wlr_input_device *dev);

#endif
