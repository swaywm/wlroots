/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_POINTER_CONSTRAINTS_V1_H
#define WLR_TYPES_WLR_POINTER_CONSTRAINTS_V1_H

#include <stdint.h>
#include <wayland-server.h>
#include <pixman.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_seat.h>
#include "pointer-constraints-unstable-v1-protocol.h"

struct wlr_seat;

enum wlr_pointer_constraint_v1_type {
	WLR_POINTER_CONSTRAINT_V1_LOCKED,
	WLR_POINTER_CONSTRAINT_V1_CONFINED,
};

enum wlr_pointer_constraint_v1_state_field {
	WLR_POINTER_CONSTRAINT_V1_STATE_REGION = 1 << 0,
	WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT = 1 << 1,
};

struct wlr_pointer_constraint_v1_state {
	uint32_t committed; // enum wlr_pointer_constraint_v1_state_field
	pixman_region32_t region;

	// only valid for locked_pointer
	struct {
		double x, y;
	} cursor_hint;
};

struct wlr_pointer_constraint_v1 {
	struct wlr_pointer_constraints_v1 *pointer_constraints;

	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_seat *seat;
	enum zwp_pointer_constraints_v1_lifetime lifetime;
	enum wlr_pointer_constraint_v1_type type;
	pixman_region32_t region;

	struct wlr_pointer_constraint_v1_state current, pending;

	struct wl_listener surface_commit;
	struct wl_listener surface_destroy;
	struct wl_listener seat_destroy;

	struct wl_list link; // wlr_pointer_constraints_v1::constraints

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

struct wlr_pointer_constraints_v1 {
	struct wl_list resources; // wl_resource_get_link
	struct wl_global *global;

	struct {
		/**
		 * Called when a new pointer constraint is created.
		 *
		 * data: struct wlr_pointer_constraint_v1 *
		 */
		struct wl_signal new_constraint;
	} events;

	struct wl_list constraints; // wlr_pointer_constraint_v1::link

	void *data;
};

struct wlr_pointer_constraints_v1 *wlr_pointer_constraints_v1_create(
	struct wl_display *display);
void wlr_pointer_constraints_v1_destroy(
	struct wlr_pointer_constraints_v1 *pointer_constraints);

struct wlr_pointer_constraint_v1 *
	wlr_pointer_constraints_v1_constraint_for_surface(
	struct wlr_pointer_constraints_v1 *pointer_constraints,
	struct wlr_surface *surface, struct wlr_seat *seat);

void wlr_pointer_constraint_v1_send_activated(
	struct wlr_pointer_constraint_v1 *constraint);
/**
 * Deactivate the constraint. May destroy the constraint.
 */
void wlr_pointer_constraint_v1_send_deactivated(
	struct wlr_pointer_constraint_v1 *constraint);

#endif
