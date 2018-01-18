#ifndef _ROOTSTON_OUTPUT_H
#define _ROOTSTON_OUTPUT_H

#include <time.h>
#include <pixman.h>
#include <wayland-server.h>

struct roots_desktop;

struct roots_output {
	struct roots_desktop *desktop;
	struct wlr_output *wlr_output;
	struct wl_list link; // roots_desktop:outputs

	struct roots_view *fullscreen_view;

	struct wl_listener frame;
	struct timespec last_frame;
	pixman_region32_t damage;
	struct wl_event_source *repaint_timer;
};

void output_add_notify(struct wl_listener *listener, void *data);
void output_remove_notify(struct wl_listener *listener, void *data);

void output_damage_surface(struct roots_output *output,
	struct wlr_surface *surface, double lx, double ly);

#endif
