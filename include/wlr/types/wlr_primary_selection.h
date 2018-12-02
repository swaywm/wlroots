/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_PRIMARY_SELECTION_H
#define WLR_TYPES_WLR_PRIMARY_SELECTION_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

struct wlr_primary_selection_source;

/**
 * A data source implementation. Only the `send` function is mandatory.
 */
struct wlr_primary_selection_source_impl {
	void (*send)(struct wlr_primary_selection_source *source,
		const char *mime_type, int fd);
	void (*destroy)(struct wlr_primary_selection_source *source);
};

/**
 * A source is the sending side of a selection.
 */
struct wlr_primary_selection_source {
	const struct wlr_primary_selection_source_impl *impl;

	// source metadata
	struct wl_array mime_types;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

void wlr_primary_selection_source_init(
	struct wlr_primary_selection_source *source,
	const struct wlr_primary_selection_source_impl *impl);
void wlr_primary_selection_source_destroy(
	struct wlr_primary_selection_source *source);
void wlr_primary_selection_source_send(
	struct wlr_primary_selection_source *source, const char *mime_type,
	int fd);

void wlr_seat_set_primary_selection(struct wlr_seat *seat,
	struct wlr_primary_selection_source *source);

#endif
