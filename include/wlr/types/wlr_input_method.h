#ifndef WLR_TYPES_WLR_INPUT_METHOD_H
#define WLR_TYPES_WLR_INPUT_METHOD_H

#include <stdint.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>

struct wlr_input_panel {
	struct wl_global *panel;
	struct wl_list surfaces; // struct wlr_input_panel_surface*
	struct {
		struct wl_signal new_surface; // struct wlr_input_panel_surface*
	} events;
};

struct wlr_input_panel_surface {
	struct wlr_surface *surface;
	struct wlr_input_panel *panel;

	struct wl_list link;

	struct {
		struct wl_signal destroy; // struct wlr_input_panel_surface*
	} events;
};

struct wlr_input_method {
	struct wl_global *input_method;
	struct wl_resource *resource;
	struct {
		struct wl_signal new_context; // struct wlr_input_method_context*
		// TODO: create roots_keyboard (seat_add_keyboard) on this; modify roots_keyboard to support backend-less
	} events;
};

struct wlr_input_method_context_preedit_string {
	const char *text;
	const char *commit;
};

struct symcode {
	xkb_keycode_t code;
	xkb_keysym_t sym;
};

struct wlr_input_method_context {
	struct wl_resource *resource;
	struct wlr_input_device input_device;
	struct wlr_keyboard *keyboard;
	struct symcode *symcodes;
	uint32_t code_counts;
	struct {
		struct wl_signal commit_string; // char*
		struct wl_signal preedit_string; // struct wlr_input_method_context_preedit_string*
		struct wl_signal destroy; // struct wlr_input_method_context*
	} events;
};

struct wlr_input_panel *wlr_input_panel_create(struct wl_display *display);

struct wlr_input_method *wlr_input_method_create(struct wl_display *display);

struct wlr_input_method_context *wlr_input_method_send_activate(
	struct wlr_input_method *input_method);

void wlr_input_method_context_send_surrounding_text(
	struct wlr_input_method_context *input_method, const char *text,
	uint32_t cursor, uint32_t anchor);

void wlr_input_method_send_deactivate(struct wlr_input_method *input_method,
	struct wlr_input_method_context *context);
#endif
