#ifndef _WLR_WAYLAND_INTERNAL_H
#define _WLR_WAYLAND_INTERNAL_H

#include <wayland-server.h>
#include <wlr/types.h>
#include <stdbool.h>

struct wlr_output_impl {
	void (*enable)(struct wlr_output_state *state, bool enable);
	bool (*set_mode)(struct wlr_output_state *state,
			struct wlr_output_mode *mode);
	void (*transform)(struct wlr_output_state *state,
			enum wl_output_transform transform);
	bool (*set_cursor)(struct wlr_output_state *state,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height);
	bool (*move_cursor)(struct wlr_output_state *state, int x, int y);
	void (*destroy)(struct wlr_output_state *state);
};

struct wlr_output *wlr_output_create(struct wlr_output_impl *impl,
		struct wlr_output_state *state);
void wlr_output_free(struct wlr_output *output);

struct wlr_keyboard_impl {
	void (*destroy)(struct wlr_keyboard_state *state);
};

struct wlr_keyboard *wlr_keyboard_create(struct wlr_keyboard_impl *impl,
		struct wlr_keyboard_state *state);
void wlr_keyboard_destroy(struct wlr_keyboard *keyboard);

struct wlr_pointer_impl {
	void (*destroy)(struct wlr_pointer_state *state);
};

struct wlr_pointer *wlr_pointer_create(struct wlr_pointer_impl *impl,
		struct wlr_pointer_state *state);
void wlr_pointer_destroy(struct wlr_pointer *pointer);

struct wlr_touch_impl {
	void (*destroy)(struct wlr_touch_state *state);
};

struct wlr_touch *wlr_touch_create(struct wlr_touch_impl *impl,
		struct wlr_touch_state *state);
void wlr_touch_destroy(struct wlr_touch *touch);

struct wlr_tablet_tool_impl {
	void (*destroy)(struct wlr_tablet_tool_state *tool);
};

struct wlr_tablet_tool *wlr_tablet_tool_create(struct wlr_tablet_tool_impl *impl,
		struct wlr_tablet_tool_state *state);
void wlr_tablet_tool_destroy(struct wlr_tablet_tool *tool);

struct wlr_input_device_impl {
	void (*destroy)(struct wlr_input_device_state *state);
};

struct wlr_input_device *wlr_input_device_create(
		enum wlr_input_device_type type,
		struct wlr_input_device_impl *impl,
		struct wlr_input_device_state *state,
		const char *name, int vendor, int product);
void wlr_input_device_destroy(struct wlr_input_device *dev);

#endif
