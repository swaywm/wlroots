#ifndef ROOTSTON_OUTPUT_H
#define ROOTSTON_OUTPUT_H
#include <pixman.h>
#include <time.h>
#include <wayland-server-core.h>
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
	struct wl_listener enable;
	struct wl_listener mode;
	struct wl_listener transform;
	struct wl_listener present;
	struct wl_listener damage_frame;
	struct wl_listener damage_destroy;
};

typedef void (*roots_surface_iterator_func_t)(struct roots_output *output,
	struct wlr_surface *surface, struct wlr_box *box, float rotation,
	void *user_data);

void rotate_child_position(double *sx, double *sy, double sw, double sh,
	double pw, double ph, float rotation);

struct roots_input;

void output_surface_for_each_surface(struct roots_output *output,
	struct wlr_surface *surface, double ox, double oy,
	roots_surface_iterator_func_t iterator, void *user_data);
void output_view_for_each_surface(struct roots_output *output,
	struct roots_view *view, roots_surface_iterator_func_t iterator,
	void *user_data);
void output_drag_icons_for_each_surface(struct roots_output *output,
	struct roots_input *input, roots_surface_iterator_func_t iterator,
	void *user_data);
void output_layer_for_each_surface(struct roots_output *output,
	struct wl_list *layer_surfaces, roots_surface_iterator_func_t iterator,
	void *user_data);
#if WLR_HAS_XWAYLAND
struct wlr_xwayland_surface;
void output_xwayland_children_for_each_surface(
	struct roots_output *output, struct wlr_xwayland_surface *surface,
	roots_surface_iterator_func_t iterator, void *user_data);
#endif
void output_for_each_surface(struct roots_output *output,
	roots_surface_iterator_func_t iterator, void *user_data);

void handle_new_output(struct wl_listener *listener, void *data);
void handle_output_manager_apply(struct wl_listener *listener, void *data);
void handle_output_manager_test(struct wl_listener *listener, void *data);

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
	struct wlr_surface *surface, double ox, double oy);
void output_damage_whole_local_surface(struct roots_output *output,
	struct wlr_surface *surface, double ox, double oy);

void output_render(struct roots_output *output);

void scale_box(struct wlr_box *box, float scale);
void get_decoration_box(struct roots_view *view,
	struct roots_output *output, struct wlr_box *box);

#endif
