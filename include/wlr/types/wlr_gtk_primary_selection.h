/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_GTK_PRIMARY_SELECTION_H
#define WLR_TYPES_WLR_GTK_PRIMARY_SELECTION_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

struct wlr_gtk_primary_selection_device_manager {
	struct wl_global *global;
	struct wl_list resources;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_gtk_primary_selection_source {
	// source metadata
	struct wl_array mime_types;

	// source implementation
	void (*send)(struct wlr_gtk_primary_selection_source *source,
		const char *mime_type, int32_t fd);
	void (*cancel)(struct wlr_gtk_primary_selection_source *source);

	// source status
	struct wlr_seat_client *seat_client;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_gtk_primary_selection_offer {
	struct wl_resource *resource;
	struct wlr_gtk_primary_selection_source *source;

	struct wl_listener source_destroy;

	void *data;
};

struct wlr_gtk_primary_selection_device_manager *
	wlr_gtk_primary_selection_device_manager_create(struct wl_display *display);
void wlr_gtk_primary_selection_device_manager_destroy(
	struct wlr_gtk_primary_selection_device_manager *manager);

void wlr_seat_client_send_gtk_primary_selection(struct wlr_seat_client *seat_client);
void wlr_seat_set_gtk_primary_selection(struct wlr_seat *seat,
	struct wlr_gtk_primary_selection_source *source, uint32_t serial);

void wlr_gtk_primary_selection_source_init(
	struct wlr_gtk_primary_selection_source *source);
void wlr_gtk_primary_selection_source_finish(
	struct wlr_gtk_primary_selection_source *source);

#endif
