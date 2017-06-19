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
bool wlr_output_set_cursor(struct wlr_output *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height);
bool wlr_output_move_cursor(struct wlr_output *output, int x, int y);
void wlr_output_destroy(struct wlr_output *output);
void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height);

enum WLR_KEYBOARD_LED {
	WLR_LED_NUM_LOCK = 1,
	WLR_LED_CAPS_LOCK = 2,
	WLR_LED_SCROLL_LOCK = 4,
	WLR_LED_LAST
};

struct wlr_keyboard_state;
struct wlr_keyboard_impl;

struct wlr_keyboard {
	struct wlr_keyboard_state *state;
	struct wlr_keyboard_impl *impl;
	uint32_t leds;

	struct {
		struct wl_signal key;
	} events;
};

void wlr_keyboard_led_update(struct wlr_keyboard *keyboard, uint32_t leds);

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
	} events;
};

struct wlr_touch_down {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_touch_up {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
};

struct wlr_touch_motion {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
	double x_mm, y_mm;
	double width_mm, height_mm;
};

struct wlr_touch_cancel {
	uint32_t time_sec;
	uint64_t time_usec;
	int32_t slot;
};

struct wlr_tablet_tool_impl;
struct wlr_tablet_tool_state;

struct wlr_tablet_tool {
	struct wlr_tablet_tool_impl *impl;
	struct wlr_tablet_tool_state *state;

	struct {
		struct wl_signal axis;
		struct wl_signal proximity;
		struct wl_signal tip;
		struct wl_signal button;
	} events;
};

enum wlr_tablet_tool_axes {
	WLR_TABLET_TOOL_AXIS_X = 1,
	WLR_TABLET_TOOL_AXIS_Y = 2,
	WLR_TABLET_TOOL_AXIS_DISTANCE = 4,
	WLR_TABLET_TOOL_AXIS_PRESSURE = 8,
	WLR_TABLET_TOOL_AXIS_TILT_X = 16,
	WLR_TABLET_TOOL_AXIS_TILT_Y = 32,
	WLR_TABLET_TOOL_AXIS_ROTATION = 64,
	WLR_TABLET_TOOL_AXIS_SLIDER = 128,
	WLR_TABLET_TOOL_AXIS_WHEEL = 256,
};

struct wlr_tablet_tool_axis {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t updated_axes;
	double x_mm, y_mm;
	double width_mm, height_mm;
	double pressure;
	double distance;
	double tilt_x, tilt_y;
	double rotation;
	double slider;
	double wheel_delta;
};

enum wlr_tablet_tool_proximity_state {
	WLR_TABLET_TOOL_PROXIMITY_OUT,
	WLR_TABLET_TOOL_PROXIMITY_IN,
};

struct wlr_tablet_tool_proximity {
	uint32_t time_sec;
	uint64_t time_usec;
	double x, y;
	double width_mm, height_mm;
	enum wlr_tablet_tool_proximity_state state;
};

enum wlr_tablet_tool_tip_state {
	WLR_TABLET_TOOL_TIP_UP,
	WLR_TABLET_TOOL_TIP_DOWN,
};

struct wlr_tablet_tool_tip {
	uint32_t time_sec;
	uint64_t time_usec;
	double x, y;
	double width_mm, height_mm;
	enum wlr_tablet_tool_tip_state state;
};

struct wlr_tablet_tool_button {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t button;
	enum wlr_button_state state;
};

// NOTE: the wlr tablet pad implementation does not currently support tablets
// with more than one mode. I don't own any such hardware so I cannot test it
// and it is too complicated to make a meaningful implementation of blindly.
struct wlr_tablet_pad_impl;
struct wlr_tablet_pad_state;

struct wlr_tablet_pad {
	struct wlr_tablet_pad_impl *impl;
	struct wlr_tablet_pad_state *state;

	struct {
		struct wl_signal button;
		struct wl_signal ring;
		struct wl_signal strip;
	} events;
};

struct wlr_tablet_pad_button {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t button;
	enum wlr_button_state state;
};

enum wlr_tablet_pad_ring_source {
	WLR_TABLET_PAD_RING_SOURCE_UNKNOWN,
	WLR_TABLET_PAD_RING_SOURCE_FINGER,
};

struct wlr_tablet_pad_ring {
	uint32_t time_sec;
	uint64_t time_usec;
	enum wlr_tablet_pad_ring_source source;
	uint32_t ring;
	double position;
};

enum wlr_tablet_pad_strip_source {
	WLR_TABLET_PAD_STRIP_SOURCE_UNKNOWN,
	WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
};

struct wlr_tablet_pad_strip {
	uint32_t time_sec;
	uint64_t time_usec;
	enum wlr_tablet_pad_strip_source source;
	uint32_t strip;
	double position;
};

enum wlr_input_device_type {
	WLR_INPUT_DEVICE_KEYBOARD,
	WLR_INPUT_DEVICE_POINTER,
	WLR_INPUT_DEVICE_TOUCH,
	WLR_INPUT_DEVICE_TABLET_TOOL,
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
		struct wlr_tablet_tool *tablet_tool;
		struct wlr_tablet_pad *tablet_pad;
	};
};

#endif
