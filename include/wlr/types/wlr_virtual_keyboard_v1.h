#ifndef WLR_TYPES_WLR_VIRTUAL_KEYBOARD_V1_H
#define WLR_TYPES_WLR_VIRTUAL_KEYBOARD_V1_H

#include <wayland-server.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>

struct wlr_virtual_keyboard_manager_v1 {
	struct wl_global *global;
	struct wl_list resources; // struct wl_resource*
	struct wl_list virtual_keyboards; // struct wlr_virtual_keyboard_v1*

	struct wl_listener display_destroy;

	struct {
		struct wl_signal new_virtual_keyboard; // struct wlr_virtual_keyboard_v1*
	} events;
};

struct wlr_virtual_keyboard_v1 {
	struct wl_resource *resource;
	struct wlr_input_device input_device;
	struct wlr_seat *seat;

	struct wl_list link;

	struct {
		struct wl_signal destroy; // struct wlr_virtual_keyboard_v1*
	} events;
};

struct wlr_virtual_keyboard_manager_v1* wlr_virtual_keyboard_manager_v1_create(
	struct wl_display *display);
void wlr_virtual_keyboard_manager_v1_destroy(
	struct wlr_virtual_keyboard_manager_v1 *manager);

#endif
