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

static void render_surface(struct wlr_surface *surface,
		struct roots_output *output, struct timespec *when,
		pixman_region32_t *damage, double lx, double ly, float rotation) {
	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	struct wlr_box box;
	bool intersects = surface_intersect_output(surface, output->desktop->layout,
		output->wlr_output, lx, ly, &box);
	if (!intersects) {
		goto render_subsurfaces;
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

render_subsurfaces:;
	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		struct wlr_surface_state *state = subsurface->surface->current;
		double sx = state->subsurface_position.x;
		double sy = state->subsurface_position.y;
		double sw = state->buffer_width / state->scale;
		double sh = state->buffer_height / state->scale;
		rotate_child_position(&sx, &sy, sw, sh, surface->current->width,
			surface->current->height, rotation);

		render_surface(subsurface->surface, output, when, damage,
			lx + sx, ly + sy, rotation);
	}
}

static void render_xdg_v6_popups(struct wlr_xdg_surface_v6 *surface,
		struct roots_output *output, struct timespec *when,
		pixman_region32_t *damage, double base_x, double base_y,
		float rotation) {
	double width = surface->surface->current->width;
	double height = surface->surface->current->height;

	struct wlr_xdg_surface_v6 *popup;
	wl_list_for_each(popup, &surface->popups, popup_link) {
		if (!popup->configured) {
			continue;
		}

		double popup_width = popup->surface->current->width;
		double popup_height = popup->surface->current->height;

		double popup_sx, popup_sy;
		wlr_xdg_surface_v6_popup_get_position(popup, &popup_sx, &popup_sy);
		rotate_child_position(&popup_sx, &popup_sy, popup_width, popup_height,
			width, height, rotation);

		render_surface(popup->surface, output, when, damage,
			base_x + popup_sx, base_y + popup_sy, rotation);
		render_xdg_v6_popups(popup, output, when, damage,
			base_x + popup_sx, base_y + popup_sy, rotation);
	}
}

static void render_wl_shell_surface(struct wlr_wl_shell_surface *surface,
		struct roots_output *output, struct timespec *when,
		pixman_region32_t *damage, double lx, double ly, float rotation,
		bool is_child) {
	if (is_child || surface->state != WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		render_surface(surface->surface, output, when, damage, lx, ly,
			rotation);

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

			render_wl_shell_surface(popup, output, when, damage,
				lx + popup_x, ly + popup_y, rotation, true);
		}
	}
}

static void render_xwayland_children(struct wlr_xwayland_surface *surface,
		struct roots_output *output, struct timespec *when,
		pixman_region32_t *damage) {
	struct wlr_xwayland_surface *child;
	wl_list_for_each(child, &surface->children, parent_link) {
		if (child->surface != NULL && child->added) {
			render_surface(child->surface, output, when, damage,
				child->x, child->y, 0);
		}
		render_xwayland_children(child, output, when, damage);
	}
}

static void render_view(struct roots_view *view, struct roots_output *output,
		struct timespec *when, pixman_region32_t *damage) {
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		render_surface(view->wlr_surface, output, when, damage,
			view->x, view->y, view->rotation);
		render_xdg_v6_popups(view->xdg_surface_v6, output, when, damage,
			view->x, view->y, view->rotation);
		break;
	case ROOTS_WL_SHELL_VIEW:
		render_wl_shell_surface(view->wl_shell_surface, output, when, damage,
			view->x, view->y, view->rotation, false);
		break;
	case ROOTS_XWAYLAND_VIEW:
		render_surface(view->wlr_surface, output, when,  damage,
			view->x, view->y, view->rotation);
		break;
	}
}

static void output_damage_whole(struct roots_output *output) {
	int width, height;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	pixman_region32_union_rect(&output->damage, &output->damage,
		0, 0, width, height);
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
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	pixman_region32_union(&output->damage, &output->damage, &wlr_output->damage);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union(&damage, &output->damage, &output->previous_damage);

	// TODO: fullscreen
	if (!pixman_region32_not_empty(&output->damage)) {
		float hz = wlr_output->refresh / 1000.0f;
		if (hz <= 0) {
			hz = 60;
		}
		wl_event_source_timer_update(output->repaint_timer, 1000.0f / hz);
		pixman_region32_clear(&output->damage);
		goto damage_finish;
	}

	wlr_log(L_DEBUG, "render");

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(server->renderer, wlr_output);
	glEnable(GL_SCISSOR_TEST);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		glScissor(rects[i].x1, wlr_output->height - rects[i].y2,
			rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1);
		glClearColor(0.25f, 0.25f, 0.25f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
	}

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

			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);

			render_view(view, output, &now, &damage);

			// During normal rendering the xwayland window tree isn't traversed
			// because all windows are rendered. Here we only want to render
			// the fullscreen window's children so we have to traverse the tree.
			if (view->type == ROOTS_XWAYLAND_VIEW) {
				render_xwayland_children(view->xwayland_surface, output, &now,
					&damage);
			}
		}

		goto renderer_end;
	} else {
		wlr_output_set_fullscreen_surface(wlr_output, NULL);
	}

	struct roots_view *view;
	wl_list_for_each_reverse(view, &desktop->views, link) {
		render_view(view, output, &now, &damage);
	}

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
				render_surface(icon, output, &now, &damage, icon_x, icon_y, 0);
			} else {
				struct wlr_touch_point *point =
					wlr_seat_touch_get_point(seat->seat, drag_icon->touch_id);
				if (point) {
					icon_x = seat->touch_x + drag_icon->sx;
					icon_y = seat->touch_y + drag_icon->sy;
					render_surface(icon, output, &now, &damage, icon_x, icon_y, 0);
				}
			}
		}
	}

renderer_end:
	glDisable(GL_SCISSOR_TEST);
	wlr_renderer_end(server->renderer);
	wlr_output_swap_buffers(wlr_output);
	pixman_region32_copy(&output->previous_damage, &output->damage);
	pixman_region32_clear(&output->damage);
	output->last_frame = desktop->last_frame = now;

damage_finish:
	pixman_region32_fini(&damage);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, frame);
	render_output(output);
}

static int handle_repaint(void *data) {
	struct roots_output *output = data;
	render_output(output);
	return 0;
}

static void output_damage_whole_surface(struct roots_output *output,
		struct wlr_surface *surface, double lx, double ly) {
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
}

void output_damage_whole_view(struct roots_output *output,
		struct roots_view *view) {
	if (view->wlr_surface != NULL) {
		output_damage_whole_surface(output, view->wlr_surface, view->x, view->y);
	}

	// TODO: subsurfaces, popups, etc
}

void output_damage_from_view(struct roots_output *output,
		struct roots_view *view) {
	// TODO: use surface damage
	output_damage_whole_view(output, view);
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
	pixman_region32_init(&output->previous_damage);
	struct wl_event_loop *ev =
		wl_display_get_event_loop(desktop->server->wl_display);
	output->repaint_timer = wl_event_loop_add_timer(ev, handle_repaint, output);

	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

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

	wl_event_source_remove(output->repaint_timer);
	pixman_region32_fini(&output->damage);
	pixman_region32_fini(&output->previous_damage);
	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	free(output);
}
