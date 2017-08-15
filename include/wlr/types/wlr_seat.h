#ifndef _WLR_TYPES_SEAT_H
#define _WLR_TYPES_SEAT_H

#include <wlr/util/list.h>
#include <wayland-server.h>

struct wlr_keyboard;
struct wlr_pointer;
struct wlr_touch;
struct wlr_data_device_manager;
struct wlr_backend;

struct wlr_seat_resources {
	struct wl_resource *seat;
	struct wl_resource *keyboard;
	struct wl_resource *pointer;
	struct wl_resource *touch;
};

struct wlr_seat {
	struct wl_global *global;

	struct {
		struct wl_signal cursor_set;
		struct wl_signal start_drag;
		struct wl_signal set_selection;
	} events;

	uint32_t caps;
	const char *name;
	list_t resources;

	struct wl_resource *pointer_over_surf;
	struct wlr_seat_resources *pointer_over_res;

	struct wl_resource *keyboard_focus_surf;
	struct wlr_seat_resources *keyboard_focus_res;

	struct {
		uint32_t keymap_format;
		int keymap_fd;
		uint32_t keymap_size;

		int32_t repeat_rate;
		int32_t repeat_delay;
	} keyboard;
};

struct wlr_seat *wlr_seat_create(struct wl_display *display, const char *name);
void wlr_seat_destroy(struct wlr_seat *seat);

void wlr_seat_set_caps(struct wlr_seat *seat, uint32_t caps);

void wlr_seat_pointer_move(struct wlr_seat *seat, uint32_t time, wl_fixed_t surface_x,
		wl_fixed_t surface_y);
void wlr_seat_pointer_enter(struct wlr_seat *seat, uint32_t serial,
		struct wl_resource *surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
void wlr_seat_pointer_leave(struct wlr_seat *seat, uint32_t serial, struct wl_resource *surface);
void wlr_set_pointer_button(struct wlr_seat *seat, uint32_t serial, uint32_t time,
		uint32_t button, uint8_t state);

void wlr_seat_keyboard_keymap(struct wlr_seat *seat, uint32_t format, int fd, uint32_t size);
void wlr_seat_keyboard_repeat_info(struct wlr_seat *seat, int32_t rate, int32_t delay);
void wlr_seat_keyboard_key(struct wlr_seat *seat, uint32_t serial, uint32_t time,
	uint32_t key, uint8_t state);
void wlr_seat_keyboard_focus(struct wlr_seat *seat, uint32_t serial, struct wl_resource *surface,
	struct wl_array *keys);
void wlr_seat_keyboard_modifiers(struct wlr_seat *seat, uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group);

struct wlr_seat_resources *wlr_seat_resources_for_client(struct wlr_seat *seat,
		struct wl_client *client);

#endif
