#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "rootston/server.h"
#include "rootston/desktop.h"
#include "rootston/config.h"

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static void render_surface(struct wlr_surface *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double lx, double ly, float rotation) {
	if (surface->texture->valid) {
		int width = surface->current->buffer_width;
		int height = surface->current->buffer_height;
		double ox = lx, oy = ly;
		wlr_output_layout_output_coords(desktop->layout, wlr_output, &ox, &oy);

		if (wlr_output_layout_intersects(desktop->layout, wlr_output,
				lx, ly, lx + width, ly + height)) {
			float matrix[16];

			float translate_origin[16];
			wlr_matrix_translate(&translate_origin,
				(int)ox + width / 2, (int)oy + height / 2, 0);
			float rotate[16];
			wlr_matrix_rotate(&rotate, rotation);
			float translate_center[16];
			wlr_matrix_translate(&translate_center, -width / 2, -height / 2, 0);
			float transform[16];
			wlr_matrix_mul(&translate_origin, &rotate, &transform);
			wlr_matrix_mul(&transform, &translate_center, &transform);
			wlr_surface_get_matrix(surface, &matrix,
					&wlr_output->transform_matrix, &transform);
			wlr_render_with_matrix(desktop->server->renderer,
					surface->texture, &matrix);

			struct wlr_frame_callback *cb, *cnext;
			wl_list_for_each_safe(cb, cnext,
				&surface->current->frame_callback_list, link) {
				wl_callback_send_done(cb->resource, timespec_to_msec(when));
				wl_resource_destroy(cb->resource);
			}
		}

		struct wlr_subsurface *subsurface;
		wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
			double sx = subsurface->surface->current->subsurface_position.x,
				sy = subsurface->surface->current->subsurface_position.y;
			double sw = subsurface->surface->current->buffer_width,
				sh = subsurface->surface->current->buffer_height;
			if (rotation != 0.0) {
				// Coordinates relative to the center of the subsurface
				double ox = sx - (double)width/2 + sw/2,
					oy = sy - (double)height/2 + sh/2;
				// Rotated coordinates
				double rx = cos(-rotation)*ox - sin(-rotation)*oy,
					ry = cos(-rotation)*oy + sin(-rotation)*ox;
				sx = rx + (double)width/2 - sw/2;
				sy = ry + (double)height/2 - sh/2;
			}

			render_surface(subsurface->surface, desktop, wlr_output, when,
				lx + sx,
				ly + sy,
				rotation);
		}
	}
}

static void render_xdg_v6_popups(struct wlr_xdg_surface_v6 *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double base_x, double base_y, float rotation) {
	// TODO: make sure this works with view rotation
	struct wlr_xdg_surface_v6 *popup;
	wl_list_for_each(popup, &surface->popups, popup_link) {
		if (!popup->configured) {
			continue;
		}

		double popup_x = base_x + surface->geometry->x +
			popup->popup_state->geometry.x - popup->geometry->x;
		double popup_y = base_y + surface->geometry->y +
			popup->popup_state->geometry.y - popup->geometry->y;
		render_surface(popup->surface, desktop, wlr_output, when, popup_x,
			popup_y, rotation);
		render_xdg_v6_popups(popup, desktop, wlr_output, when, popup_x, popup_y, rotation);
	}
}

static void render_wl_shell_surface(struct wlr_wl_shell_surface *surface, struct roots_desktop *desktop,
		struct wlr_output *wlr_output, struct timespec *when, double lx,
		double ly, float rotation, bool is_child) {
	if (is_child || surface->state != WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		render_surface(surface->surface, desktop, wlr_output, when,
			lx, ly, rotation);
		struct wlr_wl_shell_surface *popup;
		wl_list_for_each(popup, &surface->popups, popup_link) {
			render_wl_shell_surface(popup, desktop, wlr_output, when,
				lx + popup->transient_state->x,
				ly + popup->transient_state->y,
				rotation, true);
		}
	}
}

static void render_view(struct roots_view *view, struct roots_desktop *desktop,
		struct wlr_output *wlr_output, struct timespec *when) {
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		render_surface(view->wlr_surface, desktop, wlr_output, when,
			view->x, view->y, view->rotation);
		render_xdg_v6_popups(view->xdg_surface_v6, desktop, wlr_output,
			when, view->x, view->y, view->rotation);
		break;
	case ROOTS_WL_SHELL_VIEW:
		render_wl_shell_surface(view->wl_shell_surface, desktop, wlr_output,
			when, view->x, view->y, view->rotation, false);
		break;
	case ROOTS_XWAYLAND_VIEW:
		render_surface(view->wlr_surface, desktop, wlr_output, when,
			view->x, view->y, view->rotation);
		break;
	}
}

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_output *output = wl_container_of(listener, output, frame);
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(server->renderer, wlr_output);

	for (size_t i = 0; i < desktop->views->length; ++i) {
		struct roots_view *view = desktop->views->items[i];
		render_view(view, desktop, wlr_output, &now);
	}

	wlr_renderer_end(server->renderer);
	wlr_output_swap_buffers(wlr_output);

	output->last_frame = desktop->last_frame = now;
}

void output_add_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop = wl_container_of(listener, desktop, output_add);
	struct roots_input *input = desktop->server->input;
	struct roots_config *config = desktop->config;

	wlr_log(L_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(L_DEBUG, "%s %s %"PRId32"mm x %"PRId32"mm",
			wlr_output->make, wlr_output->model,
			wlr_output->phys_width, wlr_output->phys_height);
        if (wl_list_length(&wlr_output->modes) > 0) {
                struct wlr_output_mode *mode = NULL;
                mode = wl_container_of((&wlr_output->modes)->prev, mode, link);
                wlr_output_set_mode(wlr_output, mode);
        }

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	output->frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&desktop->outputs, &output->link);

	struct output_config *output_config = config_get_output(config, wlr_output);
	if (output_config) {
		wlr_output_transform(wlr_output, output_config->transform);
		wlr_output_layout_add(desktop->layout,
				wlr_output, output_config->x, output_config->y);
	} else {
		wlr_output_layout_add_auto(desktop->layout, wlr_output);
	}

	cursor_load_config(config, input->cursor, input, desktop);

	struct wlr_xcursor_image *image = input->xcursor->images[0];
	// TODO the cursor must be set depending on which surface it is displayed
	// over which should happen in the compositor.
	if (!wlr_output_set_cursor(wlr_output, image->buffer,
			image->width, image->width, image->height,
			image->hotspot_x, image->hotspot_y)) {
		wlr_log(L_DEBUG, "Failed to set hardware cursor");
		return;
	}

	wlr_cursor_warp(input->cursor, NULL, input->cursor->x, input->cursor->y);
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop = wl_container_of(listener, desktop, output_remove);
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
	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	free(output);
}
