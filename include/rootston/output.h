#ifndef ROOTSTON_OUTPUT_H
#define ROOTSTON_OUTPUT_H

#include <pixman.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output_damage.h>

struct roots_desktop;

struct roots_output {
	struct roots_desktop *desktop;
	struct wlr_output *wlr_output;
	struct wl_list link; // roots_desktop:outputs

	struct roots_view *fullscreen_view;

	struct timespec last_frame;
	struct wlr_output_damage *damage;

	struct wl_listener destroy;
	struct wl_listener frame;
};

void handle_new_output(struct wl_listener *listener, void *data);

struct roots_view;
struct roots_drag_icon;

void output_damage_whole(struct roots_output *output);
void output_damage_whole_view(struct roots_output *output,
	struct roots_view *view);
void output_damage_from_view(struct roots_output *output,
	struct roots_view *view);
void output_damage_whole_drag_icon(struct roots_output *output,
	struct roots_drag_icon *icon);

#endif
