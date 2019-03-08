/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_MANAGEMENT_V1_H
#define WLR_TYPES_WLR_OUTPUT_MANAGEMENT_V1_H

#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>

struct wlr_output_manager_v1 {
	struct wl_display *display;
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link

	struct wl_list heads; // wlr_output_head_v1::link
	uint32_t serial;

	struct {
		struct wl_signal apply; // wlr_output_configuration_v1
		struct wl_signal test; // wlr_output_configuration_v1
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_output_head_v1_state {
	struct wlr_output *output;

	bool enabled;
};

struct wlr_output_head_v1 {
	struct wlr_output_head_v1_state state;
	struct wlr_output_manager_v1 *manager;
	struct wl_list link; // wlr_output_manager_v1::heads

	struct wl_list resources; // wl_resource_get_link

	struct wl_listener output_destroy;
};

struct wlr_output_configuration_v1 {
	struct wl_list heads; // wlr_output_configuration_head_v1::link

	struct wlr_output_manager_v1 *manager;
	uint32_t serial;
	bool finalized; // client has requested to apply the config
	bool finished; // feedback has been sent by the compositor
	struct wl_resource *resource; // can be NULL
};

struct wlr_output_configuration_head_v1 {
	struct wlr_output_head_v1_state state;
	struct wlr_output_configuration_v1 *config;
	struct wl_list link; // wlr_output_configuration_v1::heads

	struct wl_resource *resource; // can be NULL

	struct wl_listener output_destroy;
};

struct wlr_output_manager_v1 *wlr_output_manager_v1_create(
	struct wl_display *display);
void wlr_output_manager_v1_set_configuration(
	struct wlr_output_manager_v1 *manager,
	struct wlr_output_configuration_v1 *config);

struct wlr_output_configuration_v1 *wlr_output_configuration_v1_create(void);
void wlr_output_configuration_v1_destroy(
	struct wlr_output_configuration_v1 *config);
void wlr_output_configuration_v1_send_succeeded(
	struct wlr_output_configuration_v1 *config);
void wlr_output_configuration_v1_send_failed(
	struct wlr_output_configuration_v1 *config);

struct wlr_output_configuration_head_v1 *
	wlr_output_configuration_head_v1_create(
	struct wlr_output_configuration_v1 *config, struct wlr_output *output);

#endif
