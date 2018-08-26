/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_GAMMA_CONTROL_H
#define WLR_TYPES_WLR_GAMMA_CONTROL_H

#include <wayland-server.h>

struct wlr_gamma_control_manager {
	struct wl_global *global;
	struct wl_list controls; // wlr_gamma_control::link

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_gamma_control {
	struct wl_resource *resource;
	struct wlr_output *output;
	struct wl_list link;

	struct wl_listener output_destroy_listener;

	struct {
		struct wl_signal destroy;
	} events;

	void* data;
};

struct wlr_gamma_control_manager *wlr_gamma_control_manager_create(
	struct wl_display *display);
void wlr_gamma_control_manager_destroy(
	struct wlr_gamma_control_manager *gamma_control_manager);

#endif
