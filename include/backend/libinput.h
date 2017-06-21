#ifndef _WLR_BACKEND_LIBINPUT_INTERNAL_H
#define _WLR_BACKEND_LIBINPUT_INTERNAL_H
#include <libinput.h>
#include <wayland-server-core.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/list.h>
#include "backend/udev.h"

struct wlr_backend_state {
	struct wlr_backend *backend;
	struct wlr_session *session;
	struct wlr_udev *udev;
	struct wl_display *display;

	struct libinput *libinput;
	struct wl_event_source *input_event;

	struct wl_listener session_signal;

	list_t *devices;
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

struct wlr_touch *wlr_libinput_touch_create(
		struct libinput_device *device);
void handle_touch_down(struct libinput_event *event,
		struct libinput_device *device);
void handle_touch_up(struct libinput_event *event,
		struct libinput_device *device);
void handle_touch_motion(struct libinput_event *event,
		struct libinput_device *device);
void handle_touch_cancel(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_tablet_tool *wlr_libinput_tablet_tool_create(
		struct libinput_device *device);
void handle_tablet_tool_axis(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_tool_proximity(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_tool_tip(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_tool_button(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_tablet_pad *wlr_libinput_tablet_pad_create(
		struct libinput_device *device);
void handle_tablet_pad_button(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_pad_ring(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_pad_strip(struct libinput_event *event,
		struct libinput_device *device);

#endif
