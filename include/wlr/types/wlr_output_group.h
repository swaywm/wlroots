/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_GROUP_H
#define WLR_TYPES_WLR_OUTPUT_GROUP_H

#include <wlr/types/wlr_output.h>

struct wlr_output_group_child;

struct wlr_output_group {
	struct wlr_output base;

	// Private state

	struct wlr_output *main_output;
	struct wl_list children; // wlr_output_group_child.link

	struct wl_listener main_output_destroy;
};

struct wlr_output_group *wlr_output_group_create(struct wlr_output *main_output);
struct wlr_output_group_child *wlr_output_group_add(
	struct wlr_output_group *group, struct wlr_output *output);
void wlr_output_group_child_destroy(struct wlr_output_group_child *child);

#endif
