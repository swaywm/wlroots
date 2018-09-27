#ifndef ROOTSTON_OUTPUT_H
#define ROOTSTON_OUTPUT_H
#include <pixman.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output_damage.h>

struct roots_desktop;

struct roots_output {
	struct roots_desktop *desktop;
	struct wlr_output *wlr_output;
	struct wl_list link; // roots_desktop:outputs

	struct roots_view *fullscreen_view;
	struct wl_list layers[4]; // layer_surface::link

	struct timespec last_frame;
	struct wlr_output_damage *damage;

	struct wlr_box usable_area;

	struct wl_listener destroy;
	struct wl_listener mode;
	struct wl_listener transform;
	struct wl_listener damage_frame;
	struct wl_listener damage_destroy;
};

void rotate_child_position(double *sx, double *sy, double sw, double sh,
	double pw, double ph, float rotation);

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
void output_damage_from_local_surface(struct roots_output *output,
	struct wlr_surface *surface, double ox, double oy, float rotation);
void output_damage_whole_local_surface(struct roots_output *output,
	struct wlr_surface *surface, double ox, double oy, float rotation);

#endif
