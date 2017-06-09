#ifndef WLR_BACKEND_LIBINPUT_H
#define WLR_BACKEND_LIBINPUT_H

#include <wayland-server.h>
#include <wlr/session.h>
#include <wlr/backend.h>
#include <wlr/backend/udev.h>

struct wlr_backend *wlr_libinput_backend_create(struct wl_display *display,
		struct wlr_session *session, struct wlr_udev *udev);

#endif
