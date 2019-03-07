/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_MANAGEMENT_V1_H
#define WLR_TYPES_WLR_OUTPUT_MANAGEMENT_V1_H

#include <wayland-server.h>
#include <wlr/types/wlr_output.h>

struct wlr_output_configuration_v1;

struct wlr_output_manager_v1 {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list resources;

	struct wlr_output_configuration_v1 *current;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;

	void *data;
};

// TODO: split this into multiple structs (state + output_head + output_configuration_head)
struct wlr_output_configuration_head_v1 {
	struct wlr_output_configuration_v1 *config;
	struct wlr_output *output;
	struct wl_list link;

	// for current config only
	struct wl_list resources;

	bool enabled;

	struct wl_listener output_destroy;
};

struct wlr_output_configuration_v1 {
	struct wl_list heads;
	uint32_t serial;
};

struct wlr_output_manager_v1 *wlr_output_manager_v1_create(
	struct wl_display *display);
void wlr_output_manager_v1_set_configuration(
	struct wlr_output_manager_v1 *manager,
	struct wlr_output_configuration_v1 *config);

struct wlr_output_configuration_v1 *wlr_output_configuration_v1_create(void);
void wlr_output_configuration_v1_destroy(
	struct wlr_output_configuration_v1 *config);

struct wlr_output_configuration_head_v1 *
	wlr_output_configuration_head_v1_create(
	struct wlr_output_configuration_v1 *config, struct wlr_output *output);

#endif
