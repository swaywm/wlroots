/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_DAMAGE_H
#define WLR_TYPES_WLR_OUTPUT_DAMAGE_H

#include <pixman.h>
#include <time.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/wlr_renderer.h>

/**
 * Damage tracking requires to keep track of previous frames' damage. To allow
 * damage tracking to work with triple buffering, a history of two frames is
 * required.
 */
#define WLR_OUTPUT_DAMAGE_PREVIOUS_LEN 2

/**
 * Tracks damage for an output.
 *
 * When a `frame` event is emitted, `wlr_output_damage_make_current` should be
 * called. If necessary, the output should be repainted and
 * `wlr_output_damage_swap_buffers` should be called. No rendering should happen
 * outside a `frame` event handler.
 */
struct wlr_output_damage {
	struct wlr_output *output;
	int max_rects; // max number of damaged rectangles

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
	struct wl_listener output_transform;
	struct wl_listener output_scale;
	struct wl_listener output_needs_swap;
	struct wl_listener output_frame;
};

struct wlr_output_damage *wlr_output_damage_create(struct wlr_output *output);
void wlr_output_damage_destroy(struct wlr_output_damage *output_damage);
/**
 * Makes the output rendering context current. `needs_swap` is set to true if
 * `wlr_output_damage_swap_buffers` needs to be called. The region of the output
 * that needs to be repainted is added to `damage`.
 */
bool wlr_output_damage_begin(struct wlr_output_damage *output_damage,
	struct wlr_renderer *renderer, bool *needs_swap, pixman_region32_t *damage);
/**
 * Swaps the output buffers. If the time of the frame isn't known, set `when` to
 * NULL.
 *
 * Swapping buffers schedules a `frame` event.
 */
bool wlr_output_damage_swap_buffers(struct wlr_output_damage *output_damage,
	struct timespec *when, pixman_region32_t *damage);
/**
 * Accumulates damage and schedules a `frame` event.
 */
void wlr_output_damage_add(struct wlr_output_damage *output_damage,
	pixman_region32_t *damage);
/**
 * Damages the whole output and schedules a `frame` event.
 */
void wlr_output_damage_add_whole(struct wlr_output_damage *output_damage);
/**
 * Accumulates damage from a box and schedules a `frame` event.
 */
void wlr_output_damage_add_box(struct wlr_output_damage *output_damage,
	struct wlr_box *box);

#endif
