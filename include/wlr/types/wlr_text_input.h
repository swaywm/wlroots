#ifndef WLR_TYPES_WLR_TEXT_INPUT_H
#define WLR_TYPES_WLR_TEXT_INPUT_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>

struct wlr_text_input_properties {
	struct {
		char *text; // NULL is allowed and equivalent to empty string
		uint32_t cursor;
		uint32_t anchor;
	} surrounding;

	struct {
		uint32_t hint;
		uint32_t purpose;
	} content_type;

	struct {
		int32_t x;
		int32_t y;
		int32_t width;
		int32_t height;
	} cursor_rectangle;
};

struct wlr_text_input {
	struct wlr_seat *seat; // used only for unlinking seat->text_input; perhaps better to use the destroy signal
	struct wl_resource *resource;
	struct wlr_surface *focused_surface;
	struct wlr_text_input_properties pending;
	struct wlr_text_input_properties current;
	struct wl_listener seat_destroy_listener;

	struct {
		struct wl_signal enable; // (uint32_t show_input_panel)
		struct wl_signal commit; // (wlr_text_input*)
		struct wl_signal disable; // (void*)
		struct wl_signal destroy; // (wlr_seat* seat)
	} events;
};

struct wlr_text_input_manager {
	struct wl_global *wl_global;
	struct wl_list text_inputs; // wlr_text_input::resource::link

	struct {
		struct wl_signal text_input; // (wlr_text_input*)
	} events;
};

struct wlr_text_input_manager *wlr_text_input_manager_create(
	struct wl_display *wl_display);
void wlr_text_input_manager_destroy(
	struct wlr_text_input_manager *manager);
void wlr_text_input_send_enter(struct wlr_text_input *text_input,
	struct wlr_surface *wlr_surface);
void wlr_text_input_send_leave(struct wlr_text_input *text_input,
	struct wlr_surface *wlr_surface);
void wlr_text_input_send_preedit_string(struct wlr_text_input *text_input,
	const char *text, uint32_t cursor);
void wlr_text_input_send_commit_string(struct wlr_text_input *text_input,
	const char *text);
void wlr_text_input_send_delete_surrounding_text(
	struct wlr_text_input *text_input, uint32_t before_length,
	uint32_t after_length);

#endif
