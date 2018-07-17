#ifndef BACKEND_LIBINPUT_H
#define BACKEND_LIBINPUT_H

#include <libinput.h>
#include <wayland-server-core.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_list.h>

struct wlr_libinput_backend {
	struct wlr_backend backend;

	struct wlr_session *session;
	struct wl_display *display;

	struct libinput *libinput_context;
	struct wl_event_source *input_event;

	struct wl_listener display_destroy;
	struct wl_listener session_signal;

	struct wlr_list wlr_device_lists; // list of struct wl_list
};

struct wlr_libinput_input_device {
	struct wlr_input_device wlr_input_device;

	struct libinput_device *handle;
};

uint32_t usec_to_msec(uint64_t usec);

void handle_libinput_event(struct wlr_libinput_backend *state,
		struct libinput_event *event);

struct wlr_input_device *get_appropriate_device(
		enum wlr_input_device_type desired_type,
		struct libinput_device *device);

struct wlr_keyboard *create_libinput_keyboard(
		struct libinput_device *device);
void handle_keyboard_key(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_pointer *create_libinput_pointer(
		struct libinput_device *device);
void handle_pointer_motion(struct libinput_event *event,
		struct libinput_device *device);
void handle_pointer_motion_abs(struct libinput_event *event,
		struct libinput_device *device);
void handle_pointer_button(struct libinput_event *event,
		struct libinput_device *device);
void handle_pointer_axis(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_touch *create_libinput_touch(
		struct libinput_device *device);
void handle_touch_down(struct libinput_event *event,
		struct libinput_device *device);
void handle_touch_up(struct libinput_event *event,
		struct libinput_device *device);
void handle_touch_motion(struct libinput_event *event,
		struct libinput_device *device);
void handle_touch_cancel(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_tablet *create_libinput_tablet(
		struct libinput_device *device);
void handle_tablet_tool_axis(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_tool_proximity(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_tool_tip(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_tool_button(struct libinput_event *event,
		struct libinput_device *device);

struct wlr_tablet_pad *create_libinput_tablet_pad(
		struct libinput_device *device);
void handle_tablet_pad_button(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_pad_ring(struct libinput_event *event,
		struct libinput_device *device);
void handle_tablet_pad_strip(struct libinput_event *event,
		struct libinput_device *device);

#endif
