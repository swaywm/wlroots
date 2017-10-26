#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_gamma_control.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include <server-decoration-protocol.h>
#include "rootston/server.h"
#include "rootston/server.h"

void view_destroy(struct roots_view *view) {
	struct roots_desktop *desktop = view->desktop;

	struct roots_input *input = desktop->server->input;
	if (input->active_view == view) {
		input->active_view = NULL;
		input->mode = ROOTS_CURSOR_PASSTHROUGH;
	}

	for (size_t i = 0; i < desktop->views->length; ++i) {
		struct roots_view *_view = desktop->views->items[i];
		if (view == _view) {
			wlr_list_del(desktop->views, i);
			break;
		}
	}
	free(view);
}

void view_get_size(const struct roots_view *view, struct wlr_box *box) {
	if (view->get_size) {
		view->get_size(view, box);
	} else {
		box->width = view->wlr_surface->current->width;
		box->height = view->wlr_surface->current->height;
	}
	box->x = view->x;
	box->y = view->y;
}

static void view_update_output(const struct roots_view *view,
		const struct wlr_box *before) {
	struct roots_desktop *desktop = view->desktop;
	struct roots_output *output;
	struct wlr_box box;
	view_get_size(view, &box);
	wl_list_for_each(output, &desktop->outputs, link) {
		bool intersected = before->x != -1 && wlr_output_layout_intersects(
				desktop->layout, output->wlr_output,
				before->x, before->y, before->x + before->width,
				before->y + before->height);
		bool intersects = wlr_output_layout_intersects(
				desktop->layout, output->wlr_output,
				view->x, view->y, view->x + box.width, view->y + box.height);
		if (intersected && !intersects) {
			wlr_log(L_DEBUG, "Leaving output %s", output->wlr_output->name);
			wlr_surface_send_leave(view->wlr_surface, output->wlr_output);
		}
		if (!intersected && intersects) {
			wlr_log(L_DEBUG, "Entering output %s", output->wlr_output->name);
			wlr_surface_send_enter(view->wlr_surface, output->wlr_output);
		}
	}
}

void view_set_position(struct roots_view *view, double x, double y) {
	struct wlr_box before;
	view_get_size(view, &before);
	if (view->set_position) {
		view->set_position(view, x, y);
	} else {
		view->x = x;
		view->y = y;
	}
	view_update_output(view, &before);
}

void view_activate(struct roots_view *view, bool activate) {
	if (view->activate) {
		view->activate(view, activate);
	}
}

void view_resize(struct roots_view *view, uint32_t width, uint32_t height) {
	struct wlr_box before;
	view_get_size(view, &before);
	if (view->resize) {
		view->resize(view, width, height);
	}
	view_update_output(view, &before);
}

void view_close(struct roots_view *view) {
	if (view->close) {
		view->close(view);
	}
}

bool view_center(struct roots_view *view) {
	struct wlr_box box;
	view_get_size(view, &box);

	struct roots_desktop *desktop = view->desktop;
	struct wlr_cursor *cursor = desktop->server->input->cursor;

	struct wlr_output *output =
		wlr_output_layout_output_at(desktop->layout, cursor->x, cursor->y);

	if (!output) {
		output = wlr_output_layout_get_center_output(desktop->layout);
	}

	if (!output) {
		// empty layout
		return false;
	}

	const struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(desktop->layout, output);

	int width, height;
	wlr_output_effective_resolution(output, &width, &height);

	double view_x = (double)(width - size.width) / 2 + l_output->x;
	double view_y = (double)(height - size.height) / 2 + l_output->y;
	view_set_position(view, view_x, view_y);

	return true;
}

void view_initialize(struct roots_view *view) {
	struct roots_input *input = view->desktop->server->input;
	view_center(view);
	set_view_focus(input, view->desktop, view);
	wlr_seat_keyboard_notify_enter(input->wl_seat, view->wlr_surface);
	view_update_output(view);
}

void view_teardown(struct roots_view *view) {
	struct wlr_list *views = view->desktop->views;
	if (views->length < 2 || views->items[views->length-1] != view) {
		return;
	}

	struct roots_view *prev_view = views->items[views->length-2];
	struct roots_input *input = prev_view->desktop->server->input;
	set_view_focus(input, prev_view->desktop, prev_view);
	wlr_seat_keyboard_notify_enter(input->wl_seat, prev_view->wlr_surface);
}

struct roots_view *view_at(struct roots_desktop *desktop, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	for (int i = desktop->views->length - 1; i >= 0; --i) {
		struct roots_view *view = desktop->views->items[i];

		if (view->type == ROOTS_WL_SHELL_VIEW &&
				view->wl_shell_surface->state ==
				WLR_WL_SHELL_SURFACE_STATE_POPUP) {
			continue;
		}

		double view_sx = lx - view->x;
		double view_sy = ly - view->y;

		struct wlr_box box = {
			.x = 0,
			.y = 0,
			.width = view->wlr_surface->current->buffer_width,
			.height = view->wlr_surface->current->buffer_height,
		};
		if (view->rotation != 0.0) {
			// Coordinates relative to the center of the view
			double ox = view_sx - (double)box.width/2,
				oy = view_sy - (double)box.height/2;
			// Rotated coordinates
			double rx = cos(view->rotation)*ox - sin(view->rotation)*oy,
				ry = cos(view->rotation)*oy + sin(view->rotation)*ox;
			view_sx = rx + (double)box.width/2;
			view_sy = ry + (double)box.height/2;
		}

		if (view->type == ROOTS_XDG_SHELL_V6_VIEW) {
			// TODO: test if this works with rotated views
			double popup_sx, popup_sy;
			struct wlr_xdg_surface_v6 *popup =
				wlr_xdg_surface_v6_popup_at(view->xdg_surface_v6,
					view_sx, view_sy, &popup_sx, &popup_sy);

			if (popup) {
				*sx = view_sx - popup_sx;
				*sy = view_sy - popup_sy;
				*surface = popup->surface;
				return view;
			}
		}

		if (view->type == ROOTS_WL_SHELL_VIEW) {
			// TODO: test if this works with rotated views
			double popup_sx, popup_sy;
			struct wlr_wl_shell_surface *popup =
				wlr_wl_shell_surface_popup_at(view->wl_shell_surface,
					view_sx, view_sy, &popup_sx, &popup_sy);

			if (popup) {
				*sx = view_sx - popup_sx;
				*sy = view_sy - popup_sy;
				*surface = popup->surface;
				return view;
			}
		}

		double sub_x, sub_y;
		struct wlr_subsurface *subsurface =
			wlr_surface_subsurface_at(view->wlr_surface,
				view_sx, view_sy, &sub_x, &sub_y);
		if (subsurface) {
			*sx = view_sx - sub_x;
			*sy = view_sy - sub_y;
			*surface = subsurface->surface;
			return view;
		}

		if (wlr_box_contains_point(&box, view_sx, view_sy) &&
				pixman_region32_contains_point(
					&view->wlr_surface->current->input,
					view_sx, view_sy, NULL)) {
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
	wlr_log(L_DEBUG, "Initializing roots desktop");

	struct roots_desktop *desktop = calloc(1, sizeof(struct roots_desktop));
	if (desktop == NULL) {
		return NULL;
	}

	desktop->views = wlr_list_create();
	if (desktop->views == NULL) {
		free(desktop);
		return NULL;
	}
	wl_list_init(&desktop->outputs);

	desktop->output_add.notify = output_add_notify;
	wl_signal_add(&server->backend->events.output_add, &desktop->output_add);
	desktop->output_remove.notify = output_remove_notify;
	wl_signal_add(&server->backend->events.output_remove,
		&desktop->output_remove);

	desktop->server = server;
	desktop->config = config;
	desktop->layout = wlr_output_layout_create();
	desktop->compositor = wlr_compositor_create(server->wl_display,
		server->renderer);

	desktop->xdg_shell_v6 = wlr_xdg_shell_v6_create(server->wl_display);
	wl_signal_add(&desktop->xdg_shell_v6->events.new_surface,
		&desktop->xdg_shell_v6_surface);
	desktop->xdg_shell_v6_surface.notify = handle_xdg_shell_v6_surface;

	desktop->wl_shell = wlr_wl_shell_create(server->wl_display);
	wl_signal_add(&desktop->wl_shell->events.new_surface,
		&desktop->wl_shell_surface);
	desktop->wl_shell_surface.notify = handle_wl_shell_surface;

#ifdef HAS_XWAYLAND
	if (config->xwayland) {
		desktop->xwayland = wlr_xwayland_create(server->wl_display,
			desktop->compositor);
		wl_signal_add(&desktop->xwayland->events.new_surface,
			&desktop->xwayland_surface);
		desktop->xwayland_surface.notify = handle_xwayland_surface;
	}
#endif

	desktop->gamma_control_manager = wlr_gamma_control_manager_create(
		server->wl_display);
	desktop->screenshooter = wlr_screenshooter_create(server->wl_display,
		server->renderer);
	desktop->server_decoration_manager =
		wlr_server_decoration_manager_create(server->wl_display);
	wlr_server_decoration_manager_set_default_mode(
		desktop->server_decoration_manager,
		ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_CLIENT);

	return desktop;
}

void desktop_destroy(struct roots_desktop *desktop) {
	// TODO
}
