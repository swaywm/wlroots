#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_gamma_control.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"

void view_destroy(struct roots_view *view) {
	struct roots_desktop *desktop = view->desktop;
	for (size_t i = 0; i < desktop->views->length; ++i) {
		struct roots_view *_view = desktop->views->items[i];
		if (view == _view) {
			list_del(desktop->views, i);
			break;
		}
	}
	free(view);
}

void view_get_size(struct roots_view *view, struct wlr_box *box) {
	if (view->get_size) {
		view->get_size(view, box);
		return;
	}
	box->x = box->y = 0;
	box->width = view->wlr_surface->current->width;
	box->height = view->wlr_surface->current->height;
}

void view_get_input_bounds(struct roots_view *view, struct wlr_box *box) {
	if (view->get_input_bounds) {
		view->get_input_bounds(view, box);
		return;
	}
	box->x = box->y = 0;
	box->width = view->wlr_surface->current->width;
	box->height = view->wlr_surface->current->height;
}

void view_activate(struct roots_view *view, bool activate) {
	if (view->activate) {
		view->activate(view, activate);
	}
}

void view_resize(struct roots_view *view, uint32_t width, uint32_t height) {
	if (view->resize) {
		view->resize(view, width, height);
	}
}

static struct wlr_subsurface *subsurface_at(struct wlr_surface *surface,
		double sx, double sy, double *sub_x, double *sub_y) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		double _sub_x = subsurface->surface->current->subsurface_position.x;
		double _sub_y = subsurface->surface->current->subsurface_position.y;
		struct wlr_subsurface *sub =
			subsurface_at(subsurface->surface, _sub_x + sx, _sub_y + sy,
				sub_x, sub_y);
		if (sub) {
			// TODO: convert sub_x and sub_y to the parent coordinate system
			return sub;
		}

		int sub_width = subsurface->surface->current->buffer_width;
		int sub_height = subsurface->surface->current->buffer_height;
		if ((sx > _sub_x && sx < _sub_x + sub_width) &&
				(sy > _sub_y && sub_y < sub_y + sub_height)) {
			*sub_x = _sub_x;
			*sub_y = _sub_y;
			return subsurface;
		}
	}

	return NULL;
}

struct roots_view *view_at(struct roots_desktop *desktop, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	for (int i = desktop->views->length - 1; i >= 0; --i) {
		struct roots_view *view = desktop->views->items[i];

		double view_sx = lx - view->x;
		double view_sy = ly - view->y;

		double sub_x, sub_y;
		struct wlr_subsurface *subsurface =
			subsurface_at(view->wlr_surface, view_sx, view_sy, &sub_x, &sub_y);

		if (subsurface) {
			*sx = view_sx - sub_x;
			*sy = view_sy - sub_y;
			*surface = subsurface->surface;
			return view;
		}

		struct wlr_box box;
		view_get_input_bounds(view, &box);
		if (view->rotation != 0.0) {
			// Coordinates relative to the center of the view
			double ox = view_sx - (double)box.width/2,
				oy = view_sy - (double)box.height/2;
			// Rotated coordinates
			double rx = cos(view->rotation)*ox - sin(view->rotation)*oy,
				ry = cos(view->rotation)*oy + sin(view->rotation)*ox;
			view_sx = (double)box.width/2 + rx;
			view_sy = (double)box.height/2 + ry;
		}

		if (wlr_box_contains_point(&box, view_sx, view_sy)) {
			*sx = view_sx;
			*sy = view_sy;
			*surface = view->wlr_surface;
			return view;
		}
	}
	return NULL;
}

struct roots_desktop *desktop_create(struct roots_server *server,
		struct roots_config *config) {
	struct roots_desktop *desktop = calloc(1, sizeof(struct roots_desktop));
	wlr_log(L_DEBUG, "Initializing roots desktop");

	assert(desktop->views = list_create());
	wl_list_init(&desktop->outputs);
	wl_list_init(&desktop->output_add.link);
	desktop->output_add.notify = output_add_notify;
	wl_list_init(&desktop->output_remove.link);
	desktop->output_remove.notify = output_remove_notify;

	wl_signal_add(&server->backend->events.output_add,
			&desktop->output_add);
	wl_signal_add(&server->backend->events.output_remove,
			&desktop->output_remove);

	desktop->server = server;
	desktop->config = config;
	desktop->layout = wlr_output_layout_create();
	desktop->compositor = wlr_compositor_create(
			server->wl_display, server->renderer);

	desktop->xdg_shell_v6 = wlr_xdg_shell_v6_create(server->wl_display);
	wl_signal_add(&desktop->xdg_shell_v6->events.new_surface,
		&desktop->xdg_shell_v6_surface);
	desktop->xdg_shell_v6_surface.notify = handle_xdg_shell_v6_surface;

	desktop->wl_shell = wlr_wl_shell_create(server->wl_display);
	wl_signal_add(&desktop->wl_shell->events.new_surface,
		&desktop->wl_shell_surface);
	desktop->wl_shell_surface.notify = handle_wl_shell_surface;

	desktop->xwayland = wlr_xwayland_create(server->wl_display,
		desktop->compositor);
	wl_signal_add(&desktop->xwayland->events.new_surface,
		&desktop->xwayland_surface);
	desktop->xwayland_surface.notify = handle_xwayland_surface;

	desktop->gamma_control_manager = wlr_gamma_control_manager_create(
			server->wl_display);

	return desktop;
}

void desktop_destroy(struct roots_desktop *desktop) {
	// TODO
}
