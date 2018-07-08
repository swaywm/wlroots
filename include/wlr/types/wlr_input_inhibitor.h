#ifndef WLR_TYPES_INPUT_INHIBITOR_H
#define WLR_TYPES_INPUT_INHIBITOR_H
#include <wayland-server.h>

struct wlr_input_inhibit_manager {
	struct wl_global *global;
	struct wl_client *active_client;
	struct wl_resource *active_inhibitor;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal activate;   // struct wlr_input_inhibit_manager *
		struct wl_signal deactivate; // struct wlr_input_inhibit_manager *
	} events;

	void *data;
};

struct wlr_input_inhibit_manager *wlr_input_inhibit_manager_create(
		struct wl_display *display);
void wlr_input_inhibit_manager_destroy(
		struct wlr_input_inhibit_manager *manager);

#endif
