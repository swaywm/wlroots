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
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include <server-decoration-protocol.h>
#include "rootston/server.h"
#include "rootston/seat.h"
#include "rootston/xcursor.h"

void view_get_box(const struct roots_view *view, struct wlr_box *box) {
	box->x = view->x;
	box->y = view->y;
	if (view->get_size) {
		view->get_size(view, box);
	} else {
		box->width = view->wlr_surface->current->width;
		box->height = view->wlr_surface->current->height;
	}
}

static void view_update_output(const struct roots_view *view,
		const struct wlr_box *before) {
	struct roots_desktop *desktop = view->desktop;
	struct roots_output *output;
	struct wlr_box box;
	view_get_box(view, &box);
	wl_list_for_each(output, &desktop->outputs, link) {
		bool intersected = before->x != -1 && wlr_output_layout_intersects(
				desktop->layout, output->wlr_output,
				before->x, before->y, before->x + before->width,
				before->y + before->height);
		bool intersects = wlr_output_layout_intersects(
				desktop->layout, output->wlr_output,
				view->x, view->y, view->x + box.width, view->y + box.height);
		if (intersected && !intersects) {
			wlr_surface_send_leave(view->wlr_surface, output->wlr_output);
		}
		if (!intersected && intersects) {
			wlr_surface_send_enter(view->wlr_surface, output->wlr_output);
		}
	}
}

void view_move(struct roots_view *view, double x, double y) {
	struct wlr_box before;
	view_get_box(view, &before);
	if (view->move) {
		view->move(view, x, y);
	} else {
		view->x = x;
		view->y = y;
	}
}

void view_activate(struct roots_view *view, bool activate) {
	if (view->activate) {
		view->activate(view, activate);
	}
}

void view_resize(struct roots_view *view, uint32_t width, uint32_t height) {
	struct wlr_box before;
	view_get_box(view, &before);
	if (view->resize) {
		view->resize(view, width, height);
	}
	view_update_output(view, &before);
}

void view_move_resize(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height) {
	if (view->move_resize) {
		view->move_resize(view, x, y, width, height);
		return;
	}

	view_move(view, x, y);
	view_resize(view, width, height);
}

void view_maximize(struct roots_view *view, bool maximized) {
	if (view->maximized == maximized) {
		return;
	}

	if (view->maximize) {
		view->maximize(view, maximized);
	}

	if (!view->maximized && maximized) {
		struct wlr_box view_box;
		view_get_box(view, &view_box);

		view->maximized = true;
		view->saved.x = view->x;
		view->saved.y = view->y;
		view->saved.rotation = view->rotation;
		view->saved.width = view_box.width;
		view->saved.height = view_box.height;

		double output_x, output_y;
		wlr_output_layout_closest_point(view->desktop->layout, NULL,
			view->x + (double)view_box.width/2,
			view->y + (double)view_box.height/2,
			&output_x, &output_y);
		struct wlr_output *output = wlr_output_layout_output_at(
			view->desktop->layout, output_x, output_y);
		struct wlr_box *output_box =
			wlr_output_layout_get_box(view->desktop->layout, output);

		view_move_resize(view, output_box->x, output_box->y, output_box->width,
			output_box->height);
		view->rotation = 0;
	}

	if (view->maximized && !maximized) {
		view->maximized = false;

		view_move_resize(view, view->saved.x, view->saved.y, view->saved.width,
			view->saved.height);
		view->rotation = view->saved.rotation;
	}
}

void view_close(struct roots_view *view) {
	if (view->close) {
		view->close(view);
	}
}

bool view_center(struct roots_view *view) {
	struct wlr_box box;
	view_get_box(view, &box);

	struct roots_desktop *desktop = view->desktop;
	struct roots_input *input = desktop->server->input;
	struct roots_seat *seat = NULL, *_seat;
	wl_list_for_each(_seat, &input->seats, link) {
		if (!seat || (seat->seat->last_event.tv_sec > _seat->seat->last_event.tv_sec &&
				seat->seat->last_event.tv_nsec > _seat->seat->last_event.tv_nsec)) {
			seat = _seat;
		}
	}
	if (!seat) {
		return false;
	}

	struct wlr_output *output =
		wlr_output_layout_output_at(desktop->layout,
				seat->cursor->cursor->x,
				seat->cursor->cursor->y);
	if (!output) {
		// empty layout
		return false;
	}

	const struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(desktop->layout, output);

	int width, height;
	wlr_output_effective_resolution(output, &width, &height);

	double view_x = (double)(width - box.width) / 2 + l_output->x;
	double view_y = (double)(height - box.height) / 2 + l_output->y;
	view_move(view, view_x, view_y);

	return true;
}

void view_destroy(struct roots_view *view) {
	wl_signal_emit(&view->events.destroy, view);

	wl_list_remove(&view->link);
	free(view);
}

void view_init(struct roots_view *view, struct roots_desktop *desktop) {
	view->desktop = desktop;
	wl_signal_init(&view->events.destroy);

	wl_list_insert(&desktop->views, &view->link);

	struct roots_seat *seat;
	wl_list_for_each(seat, &desktop->server->input->seats, link) {
		roots_seat_add_view(seat, view);
	}
}

void view_setup(struct roots_view *view) {
	struct roots_input *input = view->desktop->server->input;
	// TODO what seat gets focus? the one with the last input event?
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_focus_view(seat, view);
	}

	view_center(view);
	struct wlr_box before;
	view_get_box(view, &before);
	view_update_output(view, &before);
}

struct roots_view *view_at(struct roots_desktop *desktop, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct roots_view *view;
	wl_list_for_each(view, &desktop->views, link) {
		if (view->type == ROOTS_WL_SHELL_VIEW &&
				view->wl_shell_surface->state ==
				WLR_WL_SHELL_SURFACE_STATE_POPUP) {
			continue;
		}

		double view_sx = lx - view->x;
		double view_sy = ly - view->y;

		struct wlr_surface_state *state = view->wlr_surface->current;
		struct wlr_box box = {
			.x = 0,
			.y = 0,
			.width = state->buffer_width / state->scale,
			.height = state->buffer_height / state->scale,
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

	wl_list_init(&desktop->views);
	wl_list_init(&desktop->outputs);

	desktop->output_add.notify = output_add_notify;
	wl_signal_add(&server->backend->events.output_add, &desktop->output_add);
	desktop->output_remove.notify = output_remove_notify;
	wl_signal_add(&server->backend->events.output_remove,
		&desktop->output_remove);

	desktop->server = server;
	desktop->config = config;

	const char *cursor_theme = NULL;
	struct roots_cursor_config *cc =
		roots_config_get_cursor(config, ROOTS_CONFIG_DEFAULT_SEAT_NAME);
	if (cc != NULL) {
		cursor_theme = cc->theme;
	}

	desktop->xcursor_manager = wlr_xcursor_manager_create(cursor_theme,
		ROOTS_XCURSOR_SIZE);
	if (desktop->xcursor_manager == NULL) {
		wlr_log(L_ERROR, "Cannot create XCursor manager for theme %s",
			cursor_theme);
		free(desktop);
		return NULL;
	}

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

		if (wlr_xcursor_manager_load(desktop->xcursor_manager, 1)) {
			wlr_log(L_ERROR, "Cannot load XWayland XCursor theme");
		}
		struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
			desktop->xcursor_manager, ROOTS_XCURSOR_DEFAULT, 1);
		if (xcursor != NULL) {
			struct wlr_xcursor_image *image = xcursor->images[0];
			wlr_xwayland_set_cursor(desktop->xwayland, image->buffer,
				image->width, image->width, image->height, image->hotspot_x,
				image->hotspot_y);
		}
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
