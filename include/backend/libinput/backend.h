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

	struct libinput *libinput;
	struct wl_event_source *input_event;

	list_t *keyboards;
};

void wlr_libinput_event(struct wlr_backend_state *state,
		struct libinput_event *event);

struct wlr_keyboard_state {
	struct libinput_device *handle;
};

#endif
