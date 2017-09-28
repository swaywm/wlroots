#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdlib.h>
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
	wl_list_remove(&view->link);
	free(view);
}

void view_get_input_bounds(struct roots_view *view, struct wlr_box *box) {
	if (view->get_input_bounds) {
		view->get_input_bounds(view, box);
		return;
	}
	box->x = box->y = 0;
	box->width = view->wlr_surface->current.width;
	box->height = view->wlr_surface->current.height;
}

void view_activate(struct roots_view *view, bool activate) {
	if (view->activate) {
		view->activate(view, activate);
	}
}

struct roots_view *view_at(struct roots_desktop *desktop, int x, int y) {
	struct roots_view *view;
	wl_list_for_each(view, &desktop->views, link) {
		struct wlr_box box;
		view_get_input_bounds(view, &box);
		box.x += view->x;
		box.y += view->y;
		if (wlr_box_contains_point(&box, x, y)) {
			return view;
		}
	}
	return NULL;
}

struct roots_desktop *desktop_create(struct roots_server *server,
		struct roots_config *config) {
	struct roots_desktop *desktop = calloc(1, sizeof(struct roots_desktop));
	wlr_log(L_DEBUG, "Initializing roots desktop");

	wl_list_init(&desktop->views);
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

	wlr_cursor_attach_output_layout(server->input->cursor, desktop->layout);
	wlr_cursor_map_to_region(server->input->cursor, config->cursor.mapped_box);
	cursor_load_config(config, server->input->cursor,
			server->input, desktop);

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
