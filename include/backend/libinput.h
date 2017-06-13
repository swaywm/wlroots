#ifndef _WLR_BACKEND_LIBINPUT_INTERNAL_H
#define _WLR_BACKEND_LIBINPUT_INTERNAL_H
#include <libinput.h>
#include <wlr/backend/interface.h>
#include <wlr/common/list.h>
#include <wayland-server-core.h>
#include "backend/udev.h"
#include "types.h"

struct wlr_backend_state {
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_udev *udev;
	struct wl_display *display;

	struct libinput *libinput;
	struct wl_event_source *input_event;

	list_t *keyboards;
};

struct wlr_input_device_state {
	struct libinput_device *handle;
};

void wlr_libinput_event(struct wlr_backend_state *state,
		struct libinput_event *event);

struct wlr_input_device *get_appropriate_device(
		enum wlr_input_device_type desired_type,
		struct libinput_device *device);

struct wlr_keyboard *wlr_libinput_keyboard_create(
		struct libinput_device *device);
void handle_keyboard_key(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_pointer *wlr_libinput_pointer_create(
		struct libinput_device *device);
void handle_pointer_motion(struct libinput_event *event,
		struct libinput_device *device);
void handle_pointer_motion_abs(struct libinput_event *event,
		struct libinput_device *device);
void handle_pointer_button(struct libinput_event *event,
		struct libinput_device *device);
void handle_pointer_axis(struct libinput_event *event,
		struct libinput_device *device);

#endif
