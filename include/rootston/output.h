#ifndef _ROOTSTON_OUTPUT_H
#define _ROOTSTON_OUTPUT_H

#include <time.h>
#include <pixman.h>
#include <wayland-server.h>

/**
 * Damage tracking requires to keep track of previous frames' damage. To allow
 * damage tracking to work with triple buffering, a history of two frames is
 * required.
 */
#define ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN 2

struct roots_desktop;

struct roots_output {
	struct roots_desktop *desktop;
	struct wlr_output *wlr_output;
	struct wl_list link; // roots_desktop:outputs

	struct roots_view *fullscreen_view;

	struct timespec last_frame;
	pixman_region32_t damage; // in ouput-local coordinates

	// circular queue for previous damage
	pixman_region32_t previous_damage[ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN];
	size_t previous_damage_idx;

	struct wl_listener frame;
	struct wl_listener mode;
	struct wl_listener needs_swap;
};

void output_add_notify(struct wl_listener *listener, void *data);
void output_remove_notify(struct wl_listener *listener, void *data);

struct roots_view;
struct roots_drag_icon;

void output_damage_whole_view(struct roots_output *output,
	struct roots_view *view);
void output_damage_from_view(struct roots_output *output,
	struct roots_view *view);
void output_damage_whole_drag_icon(struct roots_output *output,
	struct roots_drag_icon *icon);

#endif
