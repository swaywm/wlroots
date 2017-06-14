#ifndef _WLR_WAYLAND_H
#define _WLR_WAYLAND_H

#include <wayland-server.h>
#include <wlr/common/list.h>
#include <stdbool.h>

struct wlr_output_mode_state;

struct wlr_output_mode {
	struct wlr_output_mode_state *state;
	uint32_t flags; // enum wl_output_mode
	int32_t width, height;
	int32_t refresh; // mHz
};

struct wlr_output_impl;
struct wlr_output_state;

struct wlr_output {
	const struct wlr_output_impl *impl;
	struct wlr_output_state *state;

	uint32_t flags;
	char name[16];
	char make[48];
	char model[16];
	uint32_t scale;
	int32_t width, height;
	int32_t phys_width, phys_height; // mm
	int32_t subpixel; // enum wl_output_subpixel
	int32_t transform; // enum wl_output_transform

	float transform_matrix[16];

	list_t *modes;
	struct wlr_output_mode *current_mode;

	struct {
		struct wl_signal frame;
	} events;
};

void wlr_output_enable(struct wlr_output *output, bool enable);
bool wlr_output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode);
void wlr_output_transform(struct wlr_output *output,
		enum wl_output_transform transform);
void wlr_output_destroy(struct wlr_output *output);
void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height);

struct wlr_keyboard_state;
struct wlr_keyboard_impl;

struct wlr_keyboard {
	struct wlr_keyboard_state *state;
	struct wlr_keyboard_impl *impl;

	struct {
		struct wl_signal key;
	} events;
};

enum wlr_key_state {
	WLR_KEY_RELEASED,
	WLR_KEY_PRESSED,
};

struct wlr_keyboard_key {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t keycode;
	enum wlr_key_state state;
};

struct wlr_pointer_state;
struct wlr_pointer_impl;

struct wlr_pointer {
	struct wlr_pointer_state *state;
	struct wlr_pointer_impl *impl;

	struct {
		struct wl_signal motion;
		struct wl_signal motion_absolute;
		struct wl_signal button;
		struct wl_signal axis;
	} events;
};

struct wlr_pointer_motion {
	uint32_t time_sec;
	uint64_t time_usec;
	double delta_x, delta_y;
};

struct wlr_pointer_motion_absolute {
	uint32_t time_sec;
	uint64_t time_usec;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

enum wlr_button_state {
	WLR_BUTTON_RELEASED,
	WLR_BUTTON_PRESSED,
};

struct wlr_pointer_button {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t button;
	enum wlr_button_state state;
};

enum wlr_axis_source {
	WLR_AXIS_SOURCE_WHEEL,
	WLR_AXIS_SOURCE_FINGER,
	WLR_AXIS_SOURCE_CONTINUOUS,
	WLR_AXIS_SOURCE_WHEEL_TILT,
};

enum wlr_axis_orientation {
	WLR_AXIS_ORIENTATION_VERTICAL,
	WLR_AXIS_ORIENTATION_HORIZONTAL,
};

struct wlr_pointer_axis {
	uint32_t time_sec;
	uint64_t time_usec;
	enum wlr_axis_source source;
	enum wlr_axis_orientation orientation;
	double delta;
};

struct wlr_touch_state;
struct wlr_touch_impl;

struct wlr_touch {
	struct wlr_touch_state *state;
	struct wlr_touch_impl *impl;

	struct {
		struct wl_signal down;
		struct wl_signal up;
		struct wl_signal motion;
		struct wl_signal cancel;
		struct wl_signal frame;
	} events;
};

// TODO: tablet & tablet tool
// TODO: gestures
// TODO: switch

enum wlr_input_device_type {
	WLR_INPUT_DEVICE_KEYBOARD,
	WLR_INPUT_DEVICE_POINTER,
	WLR_INPUT_DEVICE_TOUCH,
	WLR_INPUT_DEVICE_TABLET_PEN,
	WLR_INPUT_DEVICE_TABLET_PAD,
	WLR_INPUT_DEVICE_GESTURE,
	WLR_INPUT_DEVICE_SWITCH,
};

struct wlr_input_device_state;
struct wlr_input_device_impl;

struct wlr_input_device {
	struct wlr_input_device_state *state;
	struct wlr_input_device_impl *impl;

	enum wlr_input_device_type type;
	int vendor, product;
	char *name;

	union {
		void *_device;
		struct wlr_keyboard *keyboard;
		struct wlr_pointer *pointer;
		struct wlr_touch *touch;
	};
};

#endif
