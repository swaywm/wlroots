#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output_layout.h>
#include "rootston/desktop.h"
#include "rootston/input.h"
#include "rootston/seat.h"
#include "rootston/server.h"
#include "rootston/view.h"

struct roots_view *view_create(struct roots_desktop *desktop,
		const struct roots_view_interface *impl) {
	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	if (!view) {
		return NULL;
	}
	view->impl = impl;
	view->desktop = desktop;
	view->alpha = 1.0f;
	wl_signal_init(&view->events.unmap);
	wl_signal_init(&view->events.destroy);
	wl_list_init(&view->children);
	return view;
}

void view_get_box(const struct roots_view *view, struct wlr_box *box) {
	box->x = view->box.x;
	box->y = view->box.y;
	box->width = view->box.width;
	box->height = view->box.height;
}

void view_get_deco_box(const struct roots_view *view, struct wlr_box *box) {
	view_get_box(view, box);
	if (!view->decorated) {
		return;
	}

	box->x -= view->border_width;
	box->y -= (view->border_width + view->titlebar_height);
	box->width += view->border_width * 2;
	box->height += (view->border_width * 2 + view->titlebar_height);
}

enum roots_deco_part view_get_deco_part(struct roots_view *view, double sx,
		double sy) {
	if (!view->decorated) {
		return ROOTS_DECO_PART_NONE;
	}

	int sw = view->wlr_surface->current.width;
	int sh = view->wlr_surface->current.height;
	int bw = view->border_width;
	int titlebar_h = view->titlebar_height;

	if (sx > 0 && sx < sw && sy < 0 && sy > -view->titlebar_height) {
		return ROOTS_DECO_PART_TITLEBAR;
	}

	enum roots_deco_part parts = 0;
	if (sy >= -(titlebar_h + bw) &&
			sy <= sh + bw) {
		if (sx < 0 && sx > -bw) {
			parts |= ROOTS_DECO_PART_LEFT_BORDER;
		} else if (sx > sw && sx < sw + bw) {
			parts |= ROOTS_DECO_PART_RIGHT_BORDER;
		}
	}

	if (sx >= -bw && sx <= sw + bw) {
		if (sy > sh && sy <= sh + bw) {
			parts |= ROOTS_DECO_PART_BOTTOM_BORDER;
		} else if (sy >= -(titlebar_h + bw) && sy < 0) {
			parts |= ROOTS_DECO_PART_TOP_BORDER;
		}
	}

	// TODO corners

	return parts;
}

static void view_update_output(const struct roots_view *view,
		const struct wlr_box *before) {
	struct roots_desktop *desktop = view->desktop;

	if (view->wlr_surface == NULL) {
		return;
	}

	struct wlr_box box;
	view_get_box(view, &box);

	struct roots_output *output;
	wl_list_for_each(output, &desktop->outputs, link) {
		bool intersected = before != NULL && wlr_output_layout_intersects(
			desktop->layout, output->wlr_output, before);
		bool intersects = wlr_output_layout_intersects(desktop->layout,
			output->wlr_output, &box);
		if (intersected && !intersects) {
			wlr_surface_send_leave(view->wlr_surface, output->wlr_output);
			if (view->toplevel_handle) {
				wlr_foreign_toplevel_handle_v1_output_leave(
					view->toplevel_handle, output->wlr_output);
			}
		}
		if (!intersected && intersects) {
			wlr_surface_send_enter(view->wlr_surface, output->wlr_output);
			if (view->toplevel_handle) {
				wlr_foreign_toplevel_handle_v1_output_enter(
					view->toplevel_handle, output->wlr_output);
			}
		}
	}
}

void view_move(struct roots_view *view, double x, double y) {
	if (view->box.x == x && view->box.y == y) {
		return;
	}

	struct wlr_box before;
	view_get_box(view, &before);
	if (view->impl->move) {
		view->impl->move(view, x, y);
	} else {
		view_update_position(view, x, y);
	}
	view_update_output(view, &before);
}

void view_activate(struct roots_view *view, bool activate) {
	if (view->impl->activate) {
		view->impl->activate(view, activate);
	}

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_activated(view->toplevel_handle,
			activate);
	}
}

void view_resize(struct roots_view *view, uint32_t width, uint32_t height) {
	struct wlr_box before;
	view_get_box(view, &before);
	if (view->impl->resize) {
		view->impl->resize(view, width, height);
	}
	view_update_output(view, &before);
}

void view_move_resize(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height) {
	bool update_x = x != view->box.x;
	bool update_y = y != view->box.y;
	if (!update_x && !update_y) {
		view_resize(view, width, height);
		return;
	}

	if (view->impl->move_resize) {
		view->impl->move_resize(view, x, y, width, height);
		return;
	}

	view->pending_move_resize.update_x = update_x;
	view->pending_move_resize.update_y = update_y;
	view->pending_move_resize.x = x;
	view->pending_move_resize.y = y;
	view->pending_move_resize.width = width;
	view->pending_move_resize.height = height;

	view_resize(view, width, height);
}

static struct wlr_output *view_get_output(struct roots_view *view) {
	struct wlr_box view_box;
	view_get_box(view, &view_box);

	double output_x, output_y;
	wlr_output_layout_closest_point(view->desktop->layout, NULL,
		view->box.x + (double)view_box.width/2,
		view->box.y + (double)view_box.height/2,
		&output_x, &output_y);
	return wlr_output_layout_output_at(view->desktop->layout, output_x,
		output_y);
}

void view_arrange_maximized(struct roots_view *view) {
	if (view->fullscreen_output != NULL) {
		return;
	}

	struct wlr_box view_box;
	view_get_box(view, &view_box);

	struct wlr_output *output = view_get_output(view);
	struct roots_output *roots_output = output->data;
	struct wlr_box *output_box =
		wlr_output_layout_get_box(view->desktop->layout, output);
	struct wlr_box usable_area;
	memcpy(&usable_area, &roots_output->usable_area,
			sizeof(struct wlr_box));
	usable_area.x += output_box->x;
	usable_area.y += output_box->y;

	view_move_resize(view, usable_area.x, usable_area.y,
			usable_area.width, usable_area.height);
	view_rotate(view, 0);
}

void view_maximize(struct roots_view *view, bool maximized) {
	if (view->maximized == maximized || view->fullscreen_output != NULL) {
		return;
	}

	if (view->impl->maximize) {
		view->impl->maximize(view, maximized);
	}

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_maximized(view->toplevel_handle,
			maximized);
	}

	if (!view->maximized && maximized) {
		view->maximized = true;
		view->saved.x = view->box.x;
		view->saved.y = view->box.y;
		view->saved.rotation = view->rotation;
		view->saved.width = view->box.width;
		view->saved.height = view->box.height;

		view_arrange_maximized(view);
	}

	if (view->maximized && !maximized) {
		view->maximized = false;

		view_move_resize(view, view->saved.x, view->saved.y, view->saved.width,
			view->saved.height);
		view_rotate(view, view->saved.rotation);
	}
}

void view_set_fullscreen(struct roots_view *view, bool fullscreen,
		struct wlr_output *output) {
	bool was_fullscreen = view->fullscreen_output != NULL;
	if (was_fullscreen == fullscreen) {
		// TODO: support changing the output?
		return;
	}

	// TODO: check if client is focused?

	if (view->impl->set_fullscreen) {
		view->impl->set_fullscreen(view, fullscreen);
	}

	if (!was_fullscreen && fullscreen) {
		if (output == NULL) {
			output = view_get_output(view);
		}
		struct roots_output *roots_output =
			desktop_output_from_wlr_output(view->desktop, output);
		if (roots_output == NULL) {
			return;
		}

		struct wlr_box view_box;
		view_get_box(view, &view_box);

		view->saved.x = view->box.x;
		view->saved.y = view->box.y;
		view->saved.rotation = view->rotation;
		view->saved.width = view_box.width;
		view->saved.height = view_box.height;

		struct wlr_box *output_box =
			wlr_output_layout_get_box(view->desktop->layout, output);
		view_move_resize(view, output_box->x, output_box->y, output_box->width,
			output_box->height);
		view_rotate(view, 0);

		roots_output->fullscreen_view = view;
		view->fullscreen_output = roots_output;
		output_damage_whole(roots_output);
	}

	if (was_fullscreen && !fullscreen) {
		view_move_resize(view, view->saved.x, view->saved.y, view->saved.width,
			view->saved.height);
		view_rotate(view, view->saved.rotation);

		output_damage_whole(view->fullscreen_output);
		view->fullscreen_output->fullscreen_view = NULL;
		view->fullscreen_output = NULL;
	}
}

void view_rotate(struct roots_view *view, float rotation) {
	if (view->rotation == rotation) {
		return;
	}

	view_damage_whole(view);
	view->rotation = rotation;
	view_damage_whole(view);
}

void view_cycle_alpha(struct roots_view *view) {
	view->alpha -= 0.05;
	/* Don't go completely transparent */
	if (view->alpha < 0.1) {
		view->alpha = 1.0;
	}
	view_damage_whole(view);
}

void view_close(struct roots_view *view) {
	if (view->impl->close) {
		view->impl->close(view);
	}
}

bool view_center(struct roots_view *view) {
	struct wlr_box box;
	view_get_box(view, &box);

	struct roots_desktop *desktop = view->desktop;
	struct roots_input *input = desktop->server->input;
	struct roots_seat *seat = input_last_active_seat(input);
	if (!seat) {
		return false;
	}

	struct wlr_output *output = wlr_output_layout_output_at(desktop->layout,
		seat->cursor->cursor->x, seat->cursor->cursor->y);
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

void view_child_destroy(struct roots_view_child *child) {
	if (child == NULL) {
		return;
	}
	view_damage_whole(child->view);
	wl_list_remove(&child->link);
	wl_list_remove(&child->commit.link);
	wl_list_remove(&child->new_subsurface.link);
	child->impl->destroy(child);
}

static void view_child_handle_commit(struct wl_listener *listener,
		void *data) {
	struct roots_view_child *child = wl_container_of(listener, child, commit);
	view_apply_damage(child->view);
}

static void view_child_handle_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct roots_view_child *child =
		wl_container_of(listener, child, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	subsurface_create(child->view, wlr_subsurface);
}

void view_child_init(struct roots_view_child *child,
		const struct roots_view_child_interface *impl, struct roots_view *view,
		struct wlr_surface *wlr_surface) {
	assert(impl->destroy);
	child->impl = impl;
	child->view = view;
	child->wlr_surface = wlr_surface;
	child->commit.notify = view_child_handle_commit;
	wl_signal_add(&wlr_surface->events.commit, &child->commit);
	child->new_subsurface.notify = view_child_handle_new_subsurface;
	wl_signal_add(&wlr_surface->events.new_subsurface, &child->new_subsurface);
	wl_list_insert(&view->children, &child->link);
}

static const struct roots_view_child_interface subsurface_impl;

static void subsurface_destroy(struct roots_view_child *child) {
	assert(child->impl == &subsurface_impl);
	struct roots_subsurface *subsurface = (struct roots_subsurface *)child;
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	free(subsurface);
}

static const struct roots_view_child_interface subsurface_impl = {
	.destroy = subsurface_destroy,
};

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);
	view_child_destroy(&subsurface->view_child);
}

static void subsurface_handle_map(struct wl_listener *listener,
		void *data) {
	struct roots_subsurface *subsurface =
		wl_container_of(listener, subsurface, map);
	struct roots_view *view = subsurface->view_child.view;
	view_damage_whole(view);
	input_update_cursor_focus(view->desktop->server->input);
}

static void subsurface_handle_unmap(struct wl_listener *listener,
		void *data) {
	struct roots_subsurface *subsurface =
		wl_container_of(listener, subsurface, unmap);
	struct roots_view *view = subsurface->view_child.view;
	view_damage_whole(view);
	input_update_cursor_focus(view->desktop->server->input);
}

struct roots_subsurface *subsurface_create(struct roots_view *view,
		struct wlr_subsurface *wlr_subsurface) {
	struct roots_subsurface *subsurface =
		calloc(1, sizeof(struct roots_subsurface));
	if (subsurface == NULL) {
		return NULL;
	}
	subsurface->wlr_subsurface = wlr_subsurface;
	view_child_init(&subsurface->view_child, &subsurface_impl,
		view, wlr_subsurface->surface);
	subsurface->destroy.notify = subsurface_handle_destroy;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->map.notify = subsurface_handle_map;
	wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
	subsurface->unmap.notify = subsurface_handle_unmap;
	wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
	return subsurface;
}

void view_destroy(struct roots_view *view) {
	if (view == NULL) {
		return;
	}

	wl_signal_emit(&view->events.destroy, view);

	if (view->wlr_surface != NULL) {
		view_unmap(view);
	}

	// Can happen if fullscreened while unmapped, and hasn't been mapped
	if (view->fullscreen_output != NULL) {
		view->fullscreen_output->fullscreen_view = NULL;
	}

	if (view->impl->destroy) {
		view->impl->destroy(view);
	}

	free(view);
}

static void view_handle_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct roots_view *view = wl_container_of(listener, view, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	subsurface_create(view, wlr_subsurface);
}

void view_map(struct roots_view *view, struct wlr_surface *surface) {
	assert(view->wlr_surface == NULL);

	view->wlr_surface = surface;

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &view->wlr_surface->subsurfaces,
			parent_link) {
		subsurface_create(view, subsurface);
	}

	view->new_subsurface.notify = view_handle_new_subsurface;
	wl_signal_add(&view->wlr_surface->events.new_subsurface,
		&view->new_subsurface);

	wl_list_insert(&view->desktop->views, &view->link);
	view_damage_whole(view);
	input_update_cursor_focus(view->desktop->server->input);
}

void view_unmap(struct roots_view *view) {
	assert(view->wlr_surface != NULL);

	wl_signal_emit(&view->events.unmap, view);

	view_damage_whole(view);
	wl_list_remove(&view->link);

	wl_list_remove(&view->new_subsurface.link);

	struct roots_view_child *child, *tmp;
	wl_list_for_each_safe(child, tmp, &view->children, link) {
		view_child_destroy(child);
	}

	if (view->fullscreen_output != NULL) {
		output_damage_whole(view->fullscreen_output);
		view->fullscreen_output->fullscreen_view = NULL;
		view->fullscreen_output = NULL;
	}

	view->wlr_surface = NULL;
	view->box.width = view->box.height = 0;

	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_destroy(view->toplevel_handle);
		view->toplevel_handle = NULL;
	}
}

void view_initial_focus(struct roots_view *view) {
	struct roots_input *input = view->desktop->server->input;
	// TODO what seat gets focus? the one with the last input event?
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_set_focus(seat, view);
	}
}

void view_setup(struct roots_view *view) {
	view_initial_focus(view);

	if (view->fullscreen_output == NULL && !view->maximized) {
		view_center(view);
	}

	view_create_foreign_toplevel_handle(view);
	view_update_output(view, NULL);
}

void view_apply_damage(struct roots_view *view) {
	struct roots_output *output;
	wl_list_for_each(output, &view->desktop->outputs, link) {
		output_damage_from_view(output, view);
	}
}

void view_damage_whole(struct roots_view *view) {
	struct roots_output *output;
	wl_list_for_each(output, &view->desktop->outputs, link) {
		output_damage_whole_view(output, view);
	}
}

void view_update_position(struct roots_view *view, int x, int y) {
	if (view->box.x == x && view->box.y == y) {
		return;
	}

	view_damage_whole(view);
	view->box.x = x;
	view->box.y = y;
	view_damage_whole(view);
}

void view_update_size(struct roots_view *view, int width, int height) {
	if (view->box.width == width && view->box.height == height) {
		return;
	}

	view_damage_whole(view);
	view->box.width = width;
	view->box.height = height;
	view_damage_whole(view);
}

void view_update_decorated(struct roots_view *view, bool decorated) {
	if (view->decorated == decorated) {
		return;
	}

	view_damage_whole(view);
	view->decorated = decorated;
	if (decorated) {
		view->border_width = 4;
		view->titlebar_height = 12;
	} else {
		view->border_width = 0;
		view->titlebar_height = 0;
	}
	view_damage_whole(view);
}

void view_set_title(struct roots_view *view, const char *title) {
	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle, title);
	}
}

void view_set_app_id(struct roots_view *view, const char *app_id) {
	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle, app_id);
	}
}

static void handle_toplevel_handle_request_maximize(struct wl_listener *listener,
		void *data) {
	struct roots_view *view = wl_container_of(listener, view,
			toplevel_handle_request_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	view_maximize(view, event->maximized);
}

static void handle_toplevel_handle_request_activate(struct wl_listener *listener,
		void *data) {
	struct roots_view *view =
		wl_container_of(listener, view, toplevel_handle_request_activate);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;

	struct roots_seat *seat;
	wl_list_for_each(seat, &view->desktop->server->input->seats, link) {
		if (event->seat == seat->seat) {
			roots_seat_set_focus(seat, view);
		}
	}
}

static void handle_toplevel_handle_request_close(struct wl_listener *listener,
		void *data) {
	struct roots_view *view =
		wl_container_of(listener, view, toplevel_handle_request_close);
	view_close(view);
}

void view_create_foreign_toplevel_handle(struct roots_view *view) {
	view->toplevel_handle =
		wlr_foreign_toplevel_handle_v1_create(
			view->desktop->foreign_toplevel_manager_v1);

	view->toplevel_handle_request_maximize.notify =
		handle_toplevel_handle_request_maximize;
	wl_signal_add(&view->toplevel_handle->events.request_maximize,
			&view->toplevel_handle_request_maximize);
	view->toplevel_handle_request_activate.notify =
		handle_toplevel_handle_request_activate;
	wl_signal_add(&view->toplevel_handle->events.request_activate,
			&view->toplevel_handle_request_activate);
	view->toplevel_handle_request_close.notify =
		handle_toplevel_handle_request_close;
	wl_signal_add(&view->toplevel_handle->events.request_close,
			&view->toplevel_handle_request_close);
}
