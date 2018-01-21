#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <GLES2/gl2.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "rootston/server.h"
#include "rootston/output.h"
#include "rootston/config.h"

typedef void (*surface_iterator_func_t)(struct wlr_surface *surface,
	double lx, double ly, float rotation, void *data);

/**
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
static void rotate_child_position(double *sx, double *sy, double sw, double sh,
		double pw, double ph, float rotation) {
	if (rotation != 0.0) {
		// Coordinates relative to the center of the subsurface
		double ox = *sx - pw/2 + sw/2,
			oy = *sy - ph/2 + sh/2;
		// Rotated coordinates
		double rx = cos(-rotation)*ox - sin(-rotation)*oy,
			ry = cos(-rotation)*oy + sin(-rotation)*ox;
		*sx = rx + pw/2 - sw/2;
		*sy = ry + ph/2 - sh/2;
	}
}

static void surface_for_each_surface(struct wlr_surface *surface, double lx,
		double ly, float rotation, surface_iterator_func_t iterator,
		void *user_data) {
	iterator(surface, lx, ly, rotation, user_data);

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		struct wlr_surface_state *state = subsurface->surface->current;
		double sx = state->subsurface_position.x;
		double sy = state->subsurface_position.y;
		rotate_child_position(&sx, &sy, state->width, state->height,
			surface->current->width, surface->current->height, rotation);

		surface_for_each_surface(subsurface->surface, lx + sx, ly + sy,
			rotation, iterator, user_data);
	}
}

static void xdg_surface_v6_for_each_surface(struct wlr_xdg_surface_v6 *surface,
		double base_x, double base_y, float rotation,
		surface_iterator_func_t iterator, void *user_data) {
	double width = surface->surface->current->width;
	double height = surface->surface->current->height;

	struct wlr_xdg_popup_v6 *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface_v6 *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_width = popup->surface->current->width;
		double popup_height = popup->surface->current->height;

		double popup_sx, popup_sy;
		wlr_xdg_surface_v6_popup_get_position(popup, &popup_sx, &popup_sy);
		rotate_child_position(&popup_sx, &popup_sy, popup_width, popup_height,
			width, height, rotation);

		surface_for_each_surface(popup->surface, base_x + popup_sx,
			base_y + popup_sy, rotation, iterator, user_data);
		xdg_surface_v6_for_each_surface(popup, base_x + popup_sx,
			base_y + popup_sy, rotation, iterator, user_data);
	}
}

static void wl_shell_surface_for_each_surface(
		struct wlr_wl_shell_surface *surface, double lx, double ly,
		float rotation, bool is_child, surface_iterator_func_t iterator,
		void *user_data) {
	if (is_child || surface->state != WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		surface_for_each_surface(surface->surface, lx, ly, rotation, iterator,
			user_data);

		double width = surface->surface->current->width;
		double height = surface->surface->current->height;

		struct wlr_wl_shell_surface *popup;
		wl_list_for_each(popup, &surface->popups, popup_link) {
			double popup_width = popup->surface->current->width;
			double popup_height = popup->surface->current->height;

			double popup_x = popup->transient_state->x;
			double popup_y = popup->transient_state->y;
			rotate_child_position(&popup_x, &popup_y, popup_width, popup_height,
				width, height, rotation);

			wl_shell_surface_for_each_surface(popup, lx + popup_x, ly + popup_y,
				rotation, true, iterator, user_data);
		}
	}
}

static void view_for_each_surface(struct roots_view *view,
		surface_iterator_func_t iterator, void *user_data) {
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		surface_for_each_surface(view->wlr_surface, view->x, view->y,
			view->rotation, iterator, user_data);
		xdg_surface_v6_for_each_surface(view->xdg_surface_v6, view->x, view->y,
			view->rotation, iterator, user_data);
		break;
	case ROOTS_WL_SHELL_VIEW:
		wl_shell_surface_for_each_surface(view->wl_shell_surface, view->x,
			view->y, view->rotation, false, iterator, user_data);
		break;
	case ROOTS_XWAYLAND_VIEW:
		surface_for_each_surface(view->wlr_surface, view->x, view->y,
			view->rotation, iterator, user_data);
		break;
	}
}

static void xwayland_children_for_each_surface(
		struct wlr_xwayland_surface *surface,
		surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xwayland_surface *child;
	wl_list_for_each(child, &surface->children, parent_link) {
		if (child->surface != NULL && child->added) {
			surface_for_each_surface(child->surface, child->x, child->y, 0,
				iterator, user_data);
		}
		xwayland_children_for_each_surface(child, iterator, user_data);
	}
}


struct render_data {
	struct roots_output *output;
	struct timespec *when;
	pixman_region32_t *damage;
};

/**
 * Checks whether a surface at (lx, ly) intersects an output. Sets `box` to the
 * surface box in the output, in output-local coordinates.
 */
static bool surface_intersect_output(struct wlr_surface *surface,
		struct wlr_output_layout *output_layout, struct wlr_output *wlr_output,
		double lx, double ly, struct wlr_box *box) {
	double ox = lx, oy = ly;
	wlr_output_layout_output_coords(output_layout, wlr_output, &ox, &oy);
	box->x = ox * wlr_output->scale;
	box->y = oy * wlr_output->scale;
	box->width = surface->current->width * wlr_output->scale;
	box->height = surface->current->height * wlr_output->scale;

	struct wlr_box layout_box = {
		.x = lx, .y = ly,
		.width = surface->current->width, .height = surface->current->height,
	};
	return wlr_output_layout_intersects(output_layout, wlr_output, &layout_box);
}

static void render_surface(struct wlr_surface *surface, double lx, double ly,
		float rotation, void *_data) {
	struct render_data *data = _data;
	struct roots_output *output = data->output;
	struct timespec *when = data->when;
	pixman_region32_t *damage = data->damage;

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	struct wlr_box box;
	bool intersects = surface_intersect_output(surface, output->desktop->layout,
		output->wlr_output, lx, ly, &box);
	if (!intersects) {
		return;
	}

	// TODO: output scale, output transform support
	pixman_region32_t surface_damage;
	pixman_region32_init(&surface_damage);
	pixman_region32_union_rect(&surface_damage, &surface_damage, box.x, box.y,
		box.width, box.height);
	pixman_region32_intersect(&surface_damage, &surface_damage, damage);
	bool damaged = pixman_region32_not_empty(&surface_damage);
	if (!damaged) {
		goto surface_damage_finish;
	}

	float transform[16];
	wlr_matrix_translate(&transform, box.x, box.y, 0);

	if (rotation != 0) {
		float translate_center[16];
		wlr_matrix_translate(&translate_center, box.width/2, box.height/2, 0);

		float rotate[16];
		wlr_matrix_rotate(&rotate, rotation);

		float translate_origin[16];
		wlr_matrix_translate(&translate_origin, -box.width/2, -box.height/2, 0);

		wlr_matrix_mul(&transform, &translate_center, &transform);
		wlr_matrix_mul(&transform, &rotate, &transform);
		wlr_matrix_mul(&transform, &translate_origin, &transform);
	}

	float scale[16];
	wlr_matrix_scale(&scale, box.width, box.height, 1);

	wlr_matrix_mul(&transform, &scale, &transform);

	if (surface->current->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
		float surface_translate_center[16];
		wlr_matrix_translate(&surface_translate_center, 0.5, 0.5, 0);

		float surface_transform[16];
		wlr_matrix_transform(surface_transform,
			wlr_output_transform_invert(surface->current->transform));

		float surface_translate_origin[16];
		wlr_matrix_translate(&surface_translate_origin, -0.5, -0.5, 0);

		wlr_matrix_mul(&transform, &surface_translate_center,
			&transform);
		wlr_matrix_mul(&transform, &surface_transform, &transform);
		wlr_matrix_mul(&transform, &surface_translate_origin,
			&transform);
	}

	float matrix[16];
	wlr_matrix_mul(&output->wlr_output->transform_matrix, &transform, &matrix);

	int nrects;
	pixman_box32_t *rects =
		pixman_region32_rectangles(&surface_damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		glScissor(rects[i].x1, output->wlr_output->height - rects[i].y2,
			rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
		wlr_render_with_matrix(output->desktop->server->renderer,
			surface->texture, &matrix);
	}

	wlr_surface_send_frame_done(surface, when);

surface_damage_finish:
	pixman_region32_fini(&surface_damage);
}

static bool has_standalone_surface(struct roots_view *view) {
	if (!wl_list_empty(&view->wlr_surface->subsurface_list)) {
		return false;
	}

	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		return wl_list_empty(&view->xdg_surface_v6->popups);
	case ROOTS_WL_SHELL_VIEW:
		return wl_list_empty(&view->wl_shell_surface->popups);
	case ROOTS_XWAYLAND_VIEW:
		return wl_list_empty(&view->xwayland_surface->children);
	}
	return true;
}

static void render_output(struct roots_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;

	if (!wlr_output->enabled) {
		output->frame_pending = false;
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	float clear_color[] = {0.25f, 0.25f, 0.25f};

	// Check if we can delegate the fullscreen surface to the output
	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		// Make sure the view is centered on screen
		const struct wlr_box *output_box =
			wlr_output_layout_get_box(desktop->layout, wlr_output);
		struct wlr_box view_box;
		view_get_box(view, &view_box);
		double view_x = (double)(output_box->width - view_box.width) / 2 +
			output_box->x;
		double view_y = (double)(output_box->height - view_box.height) / 2 +
			output_box->y;
		view_move(view, view_x, view_y);

		if (has_standalone_surface(view)) {
			wlr_output_set_fullscreen_surface(wlr_output, view->wlr_surface);
		} else {
			wlr_output_set_fullscreen_surface(wlr_output, NULL);
		}

		// Fullscreen views are rendered on a black background
		clear_color[0] = clear_color[1] = clear_color[2] = 0;
	} else {
		wlr_output_set_fullscreen_surface(wlr_output, NULL);
	}

	int buffer_age = -1;
	wlr_output_make_current(wlr_output, &buffer_age);

	// Check if we can use damage tracking
	pixman_region32_t damage;
	pixman_region32_init(&damage);
	if (buffer_age <= 0 || buffer_age - 1 > ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN) {
		// Buffer new or too old, damage the whole output
		pixman_region32_union_rect(&damage, &damage, 0, 0,
			wlr_output->width, wlr_output->height);
	} else {
		pixman_region32_copy(&damage, &output->damage);

		// Accumulate damage from old buffers
		size_t idx = output->previous_damage_idx;
		for (int i = 0; i < buffer_age - 1; ++i) {
			int j = (idx + i) % ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN;
			pixman_region32_union(&damage, &damage, &output->previous_damage[j]);
		}
	}
	pixman_region32_intersect_rect(&damage, &damage, 0, 0,
		wlr_output->width, wlr_output->height);

	if (!pixman_region32_not_empty(&damage) && !wlr_output->needs_swap) {
		// Output doesn't need swap and isn't damaged, skip rendering completely
		output->frame_pending = false;
		goto damage_finish;
	}

	struct render_data data = {
		.output = output,
		.when = &now,
		.damage = &damage,
	};

	wlr_renderer_begin(server->renderer, wlr_output);
	glEnable(GL_SCISSOR_TEST);

	if (!pixman_region32_not_empty(&damage)) {
		// Output isn't damaged but needs buffer swap
		goto renderer_end;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		glScissor(rects[i].x1, wlr_output->height - rects[i].y2,
			rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
		glClearColor(clear_color[0], clear_color[1], clear_color[2], 1);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// If a view is fullscreen on this output, render it
	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		if (wlr_output->fullscreen_surface == view->wlr_surface) {
			// The output will render the fullscreen view
			goto renderer_end;
		}

		view_for_each_surface(view, render_surface, &data);

		// During normal rendering the xwayland window tree isn't traversed
		// because all windows are rendered. Here we only want to render
		// the fullscreen window's children so we have to traverse the tree.
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			xwayland_children_for_each_surface(view->xwayland_surface,
				render_surface, &data);
		}

		goto renderer_end;
	}

	// Render all views
	struct roots_view *view;
	wl_list_for_each_reverse(view, &desktop->views, link) {
		view_for_each_surface(view, render_surface, &data);
	}

	// Render drag icons
	struct wlr_drag_icon *drag_icon = NULL;
	struct roots_seat *seat = NULL;
	wl_list_for_each(seat, &server->input->seats, link) {
		wl_list_for_each(drag_icon, &seat->seat->drag_icons, link) {
			if (!drag_icon->mapped) {
				continue;
			}
			struct wlr_surface *icon = drag_icon->surface;
			struct wlr_cursor *cursor = seat->cursor->cursor;
			double icon_x = 0, icon_y = 0;
			if (drag_icon->is_pointer) {
				icon_x = cursor->x + drag_icon->sx;
				icon_y = cursor->y + drag_icon->sy;
				render_surface(icon, icon_x, icon_y, 0, &data);
			} else {
				struct wlr_touch_point *point =
					wlr_seat_touch_get_point(seat->seat, drag_icon->touch_id);
				if (point) {
					icon_x = seat->touch_x + drag_icon->sx;
					icon_y = seat->touch_y + drag_icon->sy;
					render_surface(icon, icon_x, icon_y, 0, &data);
				}
			}
		}
	}

renderer_end:
	glDisable(GL_SCISSOR_TEST);
	wlr_renderer_end(server->renderer);
	wlr_output_swap_buffers(wlr_output, &now, &damage);
	output->frame_pending = true;
	// same as decrementing, but works on unsigned integers
	output->previous_damage_idx += ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN - 1;
	output->previous_damage_idx %= ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN;
	pixman_region32_copy(&output->previous_damage[output->previous_damage_idx],
		&output->damage);
	pixman_region32_clear(&output->damage);
	output->last_frame = desktop->last_frame = now;

damage_finish:
	pixman_region32_fini(&damage);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, frame);
	render_output(output);
}

static void handle_idle_render(void *data) {
	struct roots_output *output = data;
	render_output(output);
}

static void schedule_render(struct roots_output *output) {
	if (!output->frame_pending) {
		// TODO: ask the backend to send a frame event when appropriate instead
		struct wl_event_loop *ev =
			wl_display_get_event_loop(output->desktop->server->wl_display);
		wl_event_loop_add_idle(ev, handle_idle_render, output);
		output->frame_pending = true;
	}
}

static void output_damage_whole(struct roots_output *output) {
	pixman_region32_union_rect(&output->damage, &output->damage,
		0, 0, output->wlr_output->width, output->wlr_output->height);

	schedule_render(output);
}

static void damage_whole_surface(struct wlr_surface *surface,
		double lx, double ly, float rotation, void *data) {
	struct roots_output *output = data;

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	struct wlr_box box;
	bool intersects = surface_intersect_output(surface,
		output->desktop->layout, output->wlr_output, lx, ly, &box);
	if (!intersects) {
		return;
	}

	pixman_region32_union_rect(&output->damage, &output->damage,
		box.x, box.y, box.width, box.height);

	schedule_render(output);
}

void output_damage_whole_view(struct roots_output *output,
		struct roots_view *view) {
	if (output->fullscreen_view != NULL && output->fullscreen_view != view) {
		return;
	}

	view_for_each_surface(view, damage_whole_surface, output);
}

static void damage_from_surface(struct wlr_surface *surface,
		double lx, double ly, float rotation, void *data) {
	struct roots_output *output = data;

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	struct wlr_box box;
	bool intersects = surface_intersect_output(surface,
		output->desktop->layout, output->wlr_output, lx, ly, &box);
	if (!intersects) {
		return;
	}

	// TODO: output scale, output transform support
	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_copy(&damage, &surface->current->surface_damage);
	pixman_region32_translate(&damage, box.x, box.y);
	pixman_region32_union(&output->damage, &output->damage, &damage);
	pixman_region32_fini(&damage);

	schedule_render(output);
}

void output_damage_from_view(struct roots_output *output,
		struct roots_view *view) {
	if (output->fullscreen_view != NULL && output->fullscreen_view != view) {
		return;
	}

	view_for_each_surface(view, damage_from_surface, output);
}

static void output_handle_mode(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, mode);
	output_damage_whole(output);
}

static void output_handle_needs_swap(struct wl_listener *listener, void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, needs_swap);
	pixman_region32_union(&output->damage, &output->damage,
		&output->wlr_output->damage);
	schedule_render(output);
}

static void set_mode(struct wlr_output *output,
		struct roots_output_config *oc) {
	int mhz = (int)(oc->mode.refresh_rate * 1000);

	if (wl_list_empty(&output->modes)) {
		// Output has no mode, try setting a custom one
		wlr_output_set_custom_mode(output, oc->mode.width, oc->mode.height, mhz);
		return;
	}

	struct wlr_output_mode *mode, *best = NULL;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == oc->mode.width && mode->height == oc->mode.height) {
			if (mode->refresh == mhz) {
				best = mode;
				break;
			}
			best = mode;
		}
	}
	if (!best) {
		wlr_log(L_ERROR, "Configured mode for %s not available", output->name);
	} else {
		wlr_log(L_DEBUG, "Assigning configured mode to %s", output->name);
		wlr_output_set_mode(output, best);
	}
}

void output_add_notify(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop = wl_container_of(listener, desktop,
		output_add);
	struct wlr_output *wlr_output = data;
	struct roots_input *input = desktop->server->input;
	struct roots_config *config = desktop->config;

	wlr_log(L_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(L_DEBUG, "'%s %s %s' %"PRId32"mm x %"PRId32"mm", wlr_output->make,
		wlr_output->model, wlr_output->serial, wlr_output->phys_width,
		wlr_output->phys_height);

	if (wl_list_length(&wlr_output->modes) > 0) {
		struct wlr_output_mode *mode =
			wl_container_of((&wlr_output->modes)->prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
	}

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	wl_list_insert(&desktop->outputs, &output->link);
	pixman_region32_init(&output->damage);
	for (size_t i = 0; i < ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN; ++i) {
		pixman_region32_init(&output->previous_damage[i]);
	}

	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->mode.notify = output_handle_mode;
	wl_signal_add(&wlr_output->events.mode, &output->mode);
	output->needs_swap.notify = output_handle_needs_swap;
	wl_signal_add(&wlr_output->events.needs_swap, &output->needs_swap);

	struct roots_output_config *output_config =
		roots_config_get_output(config, wlr_output);
	if (output_config) {
		if (output_config->enable) {
			if (output_config->mode.width) {
				set_mode(wlr_output, output_config);
			}
			wlr_output_set_scale(wlr_output, output_config->scale);
			wlr_output_set_transform(wlr_output, output_config->transform);
			wlr_output_layout_add(desktop->layout, wlr_output, output_config->x,
				output_config->y);
		} else {
			wlr_output_enable(wlr_output, false);
		}
	} else {
		wlr_output_layout_add_auto(desktop->layout, wlr_output);
	}

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_configure_cursor(seat);
		roots_seat_configure_xcursor(seat);
	}

	output_damage_whole(output);
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, output_remove);

	struct roots_output *output = NULL, *_output;
	wl_list_for_each(_output, &desktop->outputs, link) {
		if (_output->wlr_output == wlr_output) {
			output = _output;
			break;
		}
	}
	if (!output) {
		return; // We are unfamiliar with this output
	}

	wlr_output_layout_remove(desktop->layout, output->wlr_output);

	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);

	pixman_region32_fini(&output->damage);
	for (size_t i = 0; i < ROOTS_OUTPUT_PREVIOUS_DAMAGE_LEN; ++i) {
		pixman_region32_fini(&output->previous_damage[i]);
	}
	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->mode.link);
	wl_list_remove(&output->needs_swap.link);
	free(output);
}
