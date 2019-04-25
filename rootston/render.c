#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/config.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "rootston/layers.h"
#include "rootston/output.h"
#include "rootston/server.h"

struct render_data {
	pixman_region32_t *damage;
	float alpha;
};

static void scissor_output(struct wlr_output *wlr_output,
		pixman_box32_t *rect) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};

	int ow, oh;
	wlr_output_transformed_resolution(wlr_output, &ow, &oh);

	enum wl_output_transform transform =
		wlr_output_transform_invert(wlr_output->transform);
	wlr_box_transform(&box, &box, transform, ow, oh);

	wlr_renderer_scissor(renderer, &box);
}

static void render_texture(struct wlr_output *wlr_output,
		pixman_region32_t *output_damage, struct wlr_texture *texture,
		const struct wlr_box *box, const float matrix[static 9],
		float rotation, float alpha) {
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&rotated, box, rotation);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, rotated.x, rotated.y,
		rotated.width, rotated.height);
	pixman_region32_intersect(&damage, &damage, output_damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto buffer_damage_finish;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(wlr_output, &rects[i]);
		wlr_render_texture_with_matrix(renderer, texture, matrix, alpha);
	}

buffer_damage_finish:
	pixman_region32_fini(&damage);
}

static void render_surface_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *_box, float rotation,
		void *_data) {
	struct render_data *data = _data;
	struct wlr_output *wlr_output = output->wlr_output;
	pixman_region32_t *output_damage = data->damage;
	float alpha = data->alpha;

	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (!texture) {
		return;
	}

	struct wlr_box box = *_box;
	scale_box(&box, wlr_output->scale);

	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, rotation,
		wlr_output->transform_matrix);

	render_texture(wlr_output, output_damage,
		texture, &box, matrix, rotation, alpha);
}

static void render_decorations(struct roots_output *output,
		struct roots_view *view, struct render_data *data) {
	if (!view->decorated || view->wlr_surface == NULL) {
		return;
	}

	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(output->wlr_output->backend);
	assert(renderer);

	struct wlr_box box;
	get_decoration_box(view, output, &box);

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&rotated, &box, view->rotation);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, rotated.x, rotated.y,
		rotated.width, rotated.height);
	pixman_region32_intersect(&damage, &damage, data->damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto buffer_damage_finish;
	}

	float matrix[9];
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL,
		view->rotation, output->wlr_output->transform_matrix);
	float color[] = { 0.2, 0.2, 0.2, view->alpha };

	int nrects;
	pixman_box32_t *rects =
		pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output->wlr_output, &rects[i]);
		wlr_render_quad_with_matrix(renderer, color, matrix);
	}

buffer_damage_finish:
	pixman_region32_fini(&damage);
}

static void render_view(struct roots_output *output, struct roots_view *view,
		struct render_data *data) {
	// Do not render views fullscreened on other outputs
	if (view->fullscreen_output != NULL && view->fullscreen_output != output) {
		return;
	}

	data->alpha = view->alpha;
	if (view->fullscreen_output == NULL) {
		render_decorations(output, view, data);
	}
	output_view_for_each_surface(output, view, render_surface_iterator, data);
}

static void render_layer(struct roots_output *output,
		pixman_region32_t *damage, struct wl_list *layer_surfaces) {
	struct render_data data = {
		.damage = damage,
		.alpha = 1.0f,
	};
	output_layer_for_each_surface(output, layer_surfaces,
		render_surface_iterator, &data);
}

static void render_drag_icons(struct roots_output *output,
		pixman_region32_t *damage, struct roots_input *input) {
	struct render_data data = {
		.damage = damage,
		.alpha = 1.0f,
	};
	output_drag_icons_for_each_surface(output, input,
		render_surface_iterator, &data);
}

static void surface_send_frame_done_iterator(struct roots_output *output,
		struct wlr_surface *surface, struct wlr_box *box, float rotation,
		void *data) {
	struct timespec *when = data;
	wlr_surface_send_frame_done(surface, when);
}

void output_render(struct roots_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	if (!wlr_output->enabled) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	float clear_color[] = {0.25f, 0.25f, 0.25f, 1.0f};

	const struct wlr_box *output_box =
		wlr_output_layout_get_box(desktop->layout, wlr_output);

	// Check if we can delegate the fullscreen surface to the output
	if (output->fullscreen_view != NULL &&
			output->fullscreen_view->wlr_surface != NULL) {
		struct roots_view *view = output->fullscreen_view;

		// Make sure the view is centered on screen
		struct wlr_box view_box;
		view_get_box(view, &view_box);
		double view_x = (double)(output_box->width - view_box.width) / 2 +
			output_box->x;
		double view_y = (double)(output_box->height - view_box.height) / 2 +
			output_box->y;
		view_move(view, view_x, view_y);

		// Fullscreen views are rendered on a black background
		clear_color[0] = clear_color[1] = clear_color[2] = 0;
	}

	bool needs_frame;
	pixman_region32_t buffer_damage;
	pixman_region32_init(&buffer_damage);
	if (!wlr_output_damage_attach_render(output->damage, &needs_frame,
			&buffer_damage)) {
		return;
	}

	struct render_data data = {
		.damage = &buffer_damage,
		.alpha = 1.0,
	};

	if (!needs_frame) {
		// Output doesn't need swap and isn't damaged, skip rendering completely
		goto buffer_damage_finish;
	}

	wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);

	if (!pixman_region32_not_empty(&buffer_damage)) {
		// Output isn't damaged but needs buffer swap
		goto renderer_end;
	}

	if (server->config->debug_damage_tracking) {
		wlr_renderer_clear(renderer, (float[]){1, 1, 0, 1});
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&buffer_damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output->wlr_output, &rects[i]);
		wlr_renderer_clear(renderer, clear_color);
	}

	render_layer(output, &buffer_damage,
		&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
	render_layer(output, &buffer_damage,
		&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);

	// If a view is fullscreen on this output, render it
	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		render_view(output, view, &data);

		// During normal rendering the xwayland window tree isn't traversed
		// because all windows are rendered. Here we only want to render
		// the fullscreen window's children so we have to traverse the tree.
#if WLR_HAS_XWAYLAND
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			struct roots_xwayland_surface *xwayland_surface =
				roots_xwayland_surface_from_view(view);
			output_xwayland_children_for_each_surface(output,
				xwayland_surface->xwayland_surface,
				render_surface_iterator, &data);
		}
#endif
	} else {
		// Render all views
		struct roots_view *view;
		wl_list_for_each_reverse(view, &desktop->views, link) {
			render_view(output, view, &data);
		}
		// Render top layer above shell views
		render_layer(output, &buffer_damage,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);
	}

	render_drag_icons(output, &buffer_damage, server->input);

	render_layer(output, &buffer_damage,
		&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);

renderer_end:
	wlr_output_render_software_cursors(wlr_output, &buffer_damage);
	wlr_renderer_scissor(renderer, NULL);
	wlr_renderer_end(renderer);

	int width, height;
	wlr_output_transformed_resolution(wlr_output, &width, &height);

	pixman_region32_t output_damage;
	pixman_region32_init(&output_damage);

	if (server->config->debug_damage_tracking) {
		pixman_region32_union_rect(&output_damage, &output_damage,
			0, 0, width, height);
	}

	enum wl_output_transform transform =
		wlr_output_transform_invert(wlr_output->transform);
	wlr_region_transform(&output_damage, &output->damage->current,
		transform, width, height);

	wlr_output_set_damage(wlr_output, &output_damage);
	pixman_region32_fini(&output_damage);

	if (!wlr_output_commit(wlr_output)) {
		goto buffer_damage_finish;
	}
	output->last_frame = desktop->last_frame = now;

buffer_damage_finish:
	pixman_region32_fini(&buffer_damage);

	// Send frame done events to all surfaces
	output_for_each_surface(output, surface_send_frame_done_iterator, &now);
}
