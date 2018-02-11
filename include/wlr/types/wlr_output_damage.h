#ifndef WLR_TYPES_WLR_OUTPUT_DAMAGE_H
#define WLR_TYPES_WLR_OUTPUT_DAMAGE_H

#include <pixman.h>

/**
 * Damage tracking requires to keep track of previous frames' damage. To allow
 * damage tracking to work with triple buffering, a history of two frames is
 * required.
 */
#define WLR_OUTPUT_DAMAGE_PREVIOUS_LEN 2

struct wlr_output_damage {
	struct wlr_output *output;

	pixman_region32_t current; // in output-local coordinates

	// circular queue for previous damage
	pixman_region32_t previous[WLR_OUTPUT_DAMAGE_PREVIOUS_LEN];
	size_t previous_idx;

	struct {
		struct wl_signal frame;
		struct wl_signal destroy;
	} events;

	struct wl_listener output_destroy;
	struct wl_listener output_mode;
	struct wl_listener output_needs_swap;
	struct wl_listener output_frame;
};

struct wlr_output_damage *wlr_output_damage_create(struct wlr_output *output);
void wlr_output_damage_destroy(struct wlr_output_damage *output_damage);
bool wlr_output_damage_make_current(struct wlr_output_damage *output_damage,
	bool *needs_swap, pixman_region32_t *damage);
bool wlr_output_damage_swap_buffers(struct wlr_output_damage *output_damage,
	struct timespec *when, pixman_region32_t *damage);
void wlr_output_damage_add(struct wlr_output_damage *output_damage,
	pixman_region32_t *damage);
void wlr_output_damage_add_whole(struct wlr_output_damage *output_damage);
void wlr_output_damage_add_box(struct wlr_output_damage *output_damage,
	struct wlr_box *box);

#endif
