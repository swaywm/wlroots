/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_XCURSOR_CONFIGURATION_V1_H
#define WLR_TYPES_WLR_XCURSOR_CONFIGURATION_V1_H

#include <wayland-server.h>
#include <xcursor-configuration-unstable-v1-protocol.h>

struct wlr_xcursor_configuration_v1_attrs {
	struct {
		char *name;
		uint32_t size;
	} theme;

	char *default_cursor;
};

struct wlr_xcursor_configuration_v1 {
	struct wl_list resources; // wl_resource_get_link
	struct wlr_seat *seat;
	enum zwp_xcursor_configuration_manager_v1_device_type device_type;
	struct wl_list link;

	struct wlr_xcursor_configuration_v1_attrs attrs;

	struct wl_listener seat_destroy;

	void *data;
};

struct wlr_xcursor_configuration_manager_v1 {
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link
	struct wl_list configurations; // wlr_xcursor_configuration_v1::link

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_xcursor_configuration_manager_v1 *
	wlr_xcursor_configuration_manager_v1_create(struct wl_display *display);
void wlr_xcursor_configuration_manager_v1_destroy(
	struct wlr_xcursor_configuration_manager_v1 *manager);
void wlr_xcursor_configuration_manager_v1_configure(
	struct wlr_xcursor_configuration_manager_v1 *manager, struct wlr_seat *seat,
	enum zwp_xcursor_configuration_manager_v1_device_type device_type,
	const struct wlr_xcursor_configuration_v1_attrs *attrs);

#endif
