#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/backend/drm.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/xwayland.h>
#include "rootston/config.h"
#include "rootston/layers.h"
#include "rootston/output.h"
#include "rootston/server.h"

/**
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
void rotate_child_position(double *sx, double *sy, double sw, double sh,
		double pw, double ph, float rotation) {
	if (rotation == 0.0) {
		return;
	}

	// Coordinates relative to the center of the subsurface
	double ox = *sx - pw/2 + sw/2,
		oy = *sy - ph/2 + sh/2;
	// Rotated coordinates
	double rx = cos(rotation)*ox - sin(rotation)*oy,
		ry = cos(rotation)*oy + sin(rotation)*ox;
	*sx = rx + pw/2 - sw/2;
	*sy = ry + ph/2 - sh/2;
}

struct surface_iterator_data {
	roots_surface_iterator_func_t user_iterator;
	void *user_data;

	struct roots_output *output;
	double ox, oy;
	int width, height;
	float rotation;
};

static bool get_surface_box(struct surface_iterator_data *data,
		struct wlr_surface *surface, int sx, int sy,
		struct wlr_box *surface_box) {
	struct roots_output *output = data->output;

	if (!wlr_surface_has_buffer(surface)) {
		return false;
	}

	int sw = surface->current.width;
	int sh = surface->current.height;

	double _sx = sx + surface->sx;
	double _sy = sy + surface->sy;
	rotate_child_position(&_sx, &_sy, sw, sh, data->width, data->height,
		data->rotation);

	struct wlr_box box = {
		.x = data->ox + _sx,
		.y = data->oy + _sy,
		.width = sw,
		.height = sh,
	};
	if (surface_box != NULL) {
		*surface_box = box;
	}

	struct wlr_box rotated_box;
	wlr_box_rotated_bounds(&rotated_box, &box, data->rotation);

	struct wlr_box output_box = {0};
	wlr_output_effective_resolution(output->wlr_output,
		&output_box.width, &output_box.height);

	struct wlr_box intersection;
	return wlr_box_intersection(&intersection, &output_box, &rotated_box);
}

static void output_for_each_surface_iterator(struct wlr_surface *surface,
		int sx, int sy, void *_data) {
	struct surface_iterator_data *data = _data;

	struct wlr_box box;
	bool intersects = get_surface_box(data, surface, sx, sy, &box);
	if (!intersects) {
		return;
	}

	data->user_iterator(data->output, surface, &box, data->rotation,
		data->user_data);
}

void output_surface_for_each_surface(struct roots_output *output,
		struct wlr_surface *surface, double ox, double oy,
		roots_surface_iterator_func_t iterator, void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.ox = ox,
		.oy = oy,
		.width = surface->current.width,
		.height = surface->current.height,
		.rotation = 0,
	};

	wlr_surface_for_each_surface(surface,
		output_for_each_surface_iterator, &data);
}

void output_view_for_each_surface(struct roots_output *output,
		struct roots_view *view, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.ox = view->box.x - output->wlr_output->lx,
		.oy = view->box.y - output->wlr_output->ly,
		.width = view->box.width,
		.height = view->box.height,
		.rotation = view->rotation,
	};

	view_for_each_surface(view, output_for_each_surface_iterator, &data);
}

#if WLR_HAS_XWAYLAND
void output_xwayland_children_for_each_surface(
		struct roots_output *output, struct wlr_xwayland_surface *surface,
		roots_surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xwayland_surface *child;
	wl_list_for_each(child, &surface->children, parent_link) {
		if (child->mapped) {
			double ox = child->x - output->wlr_output->lx;
			double oy = child->y - output->wlr_output->ly;
			output_surface_for_each_surface(output, child->surface,
				ox, oy, iterator, user_data);
		}
		output_xwayland_children_for_each_surface(output, child,
			iterator, user_data);
	}
}
#endif

void output_layer_for_each_surface(struct roots_output *output,
		struct wl_list *layer_surfaces, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct roots_layer_surface *layer_surface;
	wl_list_for_each(layer_surface, layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
			layer_surface->layer_surface;
		output_surface_for_each_surface(output, wlr_layer_surface_v1->surface,
			layer_surface->geo.x, layer_surface->geo.y, iterator,
			user_data);
	}
}

void output_drag_icons_for_each_surface(struct roots_output *output,
		struct roots_input *input, roots_surface_iterator_func_t iterator,
		void *user_data) {
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		struct roots_drag_icon *drag_icon = seat->drag_icon;
		if (!drag_icon || !drag_icon->wlr_drag_icon->mapped) {
			continue;
		}

		double ox = drag_icon->x - output->wlr_output->lx;
		double oy = drag_icon->y - output->wlr_output->ly;
		output_surface_for_each_surface(output,
			drag_icon->wlr_drag_icon->surface, ox, oy, iterator, user_data);
	}
}

void output_for_each_surface(struct roots_output *output,
		roots_surface_iterator_func_t iterator, void *user_data) {
	struct roots_desktop *desktop = output->desktop;

	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		output_view_for_each_surface(output, view, iterator, user_data);

#if WLR_HAS_XWAYLAND
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			struct roots_xwayland_surface *xwayland_surface =
				roots_xwayland_surface_from_view(view);
			output_xwayland_children_for_each_surface(output,
				xwayland_surface->xwayland_surface, iterator, user_data);
		}
#endif
	} else {
		struct roots_view *view;
		wl_list_for_each_reverse(view, &desktop->views, link) {
			output_view_for_each_surface(output, view, iterator, user_data);
		}
	}

	output_drag_icons_for_each_surface(output, desktop->server->input,
		iterator, user_data);

	size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
	for (size_t i = 0; i < len; ++i) {
		output_layer_for_each_surface(output, &output->layers[i],
			iterator, user_data);
	}
}

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

void get_decoration_box(struct roots_view *view,
		struct roots_output *output, struct wlr_box *box) {
	struct wlr_output *wlr_output = output->wlr_output;

	struct wlr_box deco_box;
	view_get_deco_box(view, &deco_box);
	double sx = deco_box.x - view->box.x;
	double sy = deco_box.y - view->box.y;
	rotate_child_position(&sx, &sy, deco_box.width, deco_box.height,
		view->wlr_surface->current.width,
		view->wlr_surface->current.height, view->rotation);
	double x = sx + view->box.x;
	double y = sy + view->box.y;

	wlr_output_layout_output_coords(output->desktop->layout, wlr_output, &x, &y);

	box->x = x * wlr_output->scale;
	box->y = y * wlr_output->scale;
	box->width = deco_box.width * wlr_output->scale;
	box->height = deco_box.height * wlr_output->scale;
}

void output_damage_whole(struct roots_output *output) {
	wlr_output_damage_add_whole(output->damage);
}

static bool view_accept_damage(struct roots_output *output,
		struct roots_view *view) {
	if (view->wlr_surface == NULL) {
		return false;
	}
	if (output->fullscreen_view == NULL) {
		return true;
	}
	if (output->fullscreen_view == view) {
		return true;
	}
#if WLR_HAS_XWAYLAND
	if (output->fullscreen_view->type == ROOTS_XWAYLAND_VIEW &&
			view->type == ROOTS_XWAYLAND_VIEW) {
		// Special case: accept damage from children
		struct wlr_xwayland_surface *xsurface =
			roots_xwayland_surface_from_view(view)->xwayland_surface;
		struct wlr_xwayland_surface *fullscreen_xsurface =
			roots_xwayland_surface_from_view(output->fullscreen_view)->xwayland_surface;
		while (xsurface != NULL) {
			if (fullscreen_xsurface == xsurface) {
				return true;
			}
			xsurface = xsurface->parent;
		}
	}
#endif
	return false;
}

static void damage_surface_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		void *data) {
	bool *whole = data;

	struct wlr_box box = *_box;
	scale_box(&box, output->wlr_output->scale);

	int center_x = box.x + box.width/2;
	int center_y = box.y + box.height/2;

	if (pixman_region32_not_empty(&surface->buffer_damage)) {
		pixman_region32_t damage;
		pixman_region32_init(&damage);
		wlr_surface_get_effective_damage(surface, &damage);
		wlr_region_scale(&damage, &damage, output->wlr_output->scale);
		if (ceil(output->wlr_output->scale) > surface->current.scale) {
			// When scaling up a surface, it'll become blurry so we need to
			// expand the damage region
			wlr_region_expand(&damage, &damage,
				ceil(output->wlr_output->scale) - surface->current.scale);
		}
		pixman_region32_translate(&damage, box.x, box.y);
		wlr_region_rotated_bounds(&damage, &damage, rotation,
			center_x, center_y);
		wlr_output_damage_add(output->damage, &damage);
		pixman_region32_fini(&damage);
	}

	if (*whole) {
		wlr_box_rotated_bounds(&box, &box, rotation);
		wlr_output_damage_add_box(output->damage, &box);
	}

	wlr_output_schedule_frame(output->wlr_output);
}

void output_damage_whole_local_surface(struct roots_output *output,
		struct wlr_surface *surface, double ox, double oy) {
	bool whole = true;
	output_surface_for_each_surface(output, surface, ox, oy,
		damage_surface_iterator, &whole);
}

static void damage_whole_decoration(struct roots_view *view,
		struct roots_output *output) {
	if (!view->decorated || view->wlr_surface == NULL) {
		return;
	}

	struct wlr_box box;
	get_decoration_box(view, output, &box);

	wlr_box_rotated_bounds(&box, &box, view->rotation);

	wlr_output_damage_add_box(output->damage, &box);
}

void output_damage_whole_view(struct roots_output *output,
		struct roots_view *view) {
	if (!view_accept_damage(output, view)) {
		return;
	}

	damage_whole_decoration(view, output);

	bool whole = true;
	output_view_for_each_surface(output, view, damage_surface_iterator, &whole);
}

void output_damage_whole_drag_icon(struct roots_output *output,
		struct roots_drag_icon *icon) {
	bool whole = true;
	output_surface_for_each_surface(output, icon->wlr_drag_icon->surface,
		icon->x, icon->y, damage_surface_iterator, &whole);
}

void output_damage_from_local_surface(struct roots_output *output,
		struct wlr_surface *surface, double ox, double oy) {
	bool whole = false;
	output_surface_for_each_surface(output, surface, ox, oy,
		damage_surface_iterator, &whole);
}

void output_damage_from_view(struct roots_output *output,
		struct roots_view *view) {
	if (!view_accept_damage(output, view)) {
		return;
	}

	bool whole = false;
	output_view_for_each_surface(output, view, damage_surface_iterator, &whole);
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
		wlr_log(WLR_ERROR, "Configured mode for %s not available", output->name);
	} else {
		wlr_log(WLR_DEBUG, "Assigning configured mode to %s", output->name);
		wlr_output_set_mode(output, best);
	}
}

static void output_destroy(struct roots_output *output) {
	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);

	wl_list_remove(&output->link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->mode.link);
	wl_list_remove(&output->transform.link);
	wl_list_remove(&output->present.link);
	wl_list_remove(&output->damage_frame.link);
	wl_list_remove(&output->damage_destroy.link);
	free(output);
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, destroy);
	output_destroy(output);
}

static void output_damage_handle_frame(struct wl_listener *listener,
		void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, damage_frame);
	output_render(output);
}

static void output_damage_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, damage_destroy);
	output_destroy(output);
}

static void output_handle_mode(struct wl_listener *listener, void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, mode);
	arrange_layers(output);
}

static void output_handle_transform(struct wl_listener *listener, void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, transform);
	arrange_layers(output);
}

static void surface_send_presented_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		void *data) {
	struct wlr_presentation_event *event = data;
	wlr_presentation_send_surface_presented(output->desktop->presentation,
		surface, event);
}

static void output_handle_present(struct wl_listener *listener, void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, present);
	struct wlr_output_event_present *output_event = data;

	struct wlr_presentation_event event = {
		.output = output->wlr_output,
		.tv_sec = (uint64_t)output_event->when->tv_sec,
		.tv_nsec = (uint32_t)output_event->when->tv_nsec,
		.refresh = (uint32_t)output_event->refresh,
		.seq = (uint64_t)output_event->seq,
		.flags = output_event->flags,
	};

	output_for_each_surface(output,
		surface_send_presented_iterator, &event);
}

void handle_new_output(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop = wl_container_of(listener, desktop,
		new_output);
	struct wlr_output *wlr_output = data;
	struct roots_input *input = desktop->server->input;
	struct roots_config *config = desktop->config;

	wlr_log(WLR_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(WLR_DEBUG, "'%s %s %s' %"PRId32"mm x %"PRId32"mm", wlr_output->make,
		wlr_output->model, wlr_output->serial, wlr_output->phys_width,
		wlr_output->phys_height);

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	wlr_output->data = output;
	wl_list_insert(&desktop->outputs, &output->link);

	output->damage = wlr_output_damage_create(wlr_output);

	output->destroy.notify = output_handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->mode.notify = output_handle_mode;
	wl_signal_add(&wlr_output->events.mode, &output->mode);
	output->transform.notify = output_handle_transform;
	wl_signal_add(&wlr_output->events.transform, &output->transform);
	output->present.notify = output_handle_present;
	wl_signal_add(&wlr_output->events.present, &output->present);

	output->damage_frame.notify = output_damage_handle_frame;
	wl_signal_add(&output->damage->events.frame, &output->damage_frame);
	output->damage_destroy.notify = output_damage_handle_destroy;
	wl_signal_add(&output->damage->events.destroy, &output->damage_destroy);

	size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
	for (size_t i = 0; i < len; ++i) {
		wl_list_init(&output->layers[i]);
	}

	struct roots_output_config *output_config =
		roots_config_get_output(config, wlr_output);

	if ((!output_config || output_config->enable) && !wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode =
			wl_container_of(wlr_output->modes.prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
	}

	if (output_config) {
		if (output_config->enable) {
			if (wlr_output_is_drm(wlr_output)) {
				struct roots_output_mode_config *mode_config;
				wl_list_for_each(mode_config, &output_config->modes, link) {
					wlr_drm_connector_add_mode(wlr_output, &mode_config->info);
				}
			} else if (!wl_list_empty(&output_config->modes)) {
				wlr_log(WLR_ERROR, "Can only add modes for DRM backend");
			}

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

	arrange_layers(output);
	output_damage_whole(output);
}
