#ifndef _WLR_BACKEND_LIBINPUT_INTERNAL_H
#define _WLR_BACKEND_LIBINPUT_INTERNAL_H
#include <libinput.h>
#include <wlr/backend/interface.h>
#include <wlr/common/list.h>
#include <wayland-server-core.h>
#include "backend/udev.h"

struct wlr_backend_state {
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_udev *udev;
	struct wl_display *display;

	struct libinput *handle;
	struct wl_event_source *input_event;

	list_t *devices;
};

#endif
