#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/region.h>
#include "util/signal.h"

static struct wlr_scene *scene_root_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_ROOT);
	return (struct wlr_scene *)node;
}

struct wlr_scene_surface *wlr_scene_surface_from_node(
		struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_SURFACE);
	return (struct wlr_scene_surface *)node;
}

static struct wlr_scene_rect *scene_rect_from_node(
		struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_RECT);
	return (struct wlr_scene_rect *)node;
}

static void scene_node_state_init(struct wlr_scene_node_state *state) {
	wl_list_init(&state->children);
	wl_list_init(&state->link);
	state->enabled = true;
}

static void scene_node_state_finish(struct wlr_scene_node_state *state) {
	wl_list_remove(&state->link);
}

static void scene_node_init(struct wlr_scene_node *node,
		enum wlr_scene_node_type type, struct wlr_scene_node *parent) {
	assert(type == WLR_SCENE_NODE_ROOT || parent != NULL);

	node->type = type;
	node->parent = parent;
	scene_node_state_init(&node->state);
	wl_signal_init(&node->events.destroy);

	if (parent != NULL) {
		wl_list_insert(parent->state.children.prev, &node->state.link);
	}
}

static void scene_node_finish(struct wlr_scene_node *node) {
	wlr_signal_emit_safe(&node->events.destroy, NULL);

	struct wlr_scene_node *child, *child_tmp;
	wl_list_for_each_safe(child, child_tmp,
			&node->state.children, state.link) {
		wlr_scene_node_destroy(child);
	}

	scene_node_state_finish(&node->state);
}

static void scene_node_damage_whole(struct wlr_scene_node *node);
static struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node);

void wlr_scene_node_destroy(struct wlr_scene_node *node) {
	if (node == NULL) {
		return;
	}

	scene_node_damage_whole(node);
	scene_node_finish(node);

	struct wlr_scene *scene = scene_node_get_root(node);
	struct wlr_scene_output *scene_output;
	switch (node->type) {
	case WLR_SCENE_NODE_ROOT:;
		struct wlr_scene_output *scene_output_tmp;
		wl_list_for_each_safe(scene_output, scene_output_tmp, &scene->outputs, link) {
			wlr_scene_output_destroy(scene_output);
		}

		free(scene);
		break;
	case WLR_SCENE_NODE_SURFACE:;
		struct wlr_scene_surface *scene_surface = wlr_scene_surface_from_node(node);

		wl_list_for_each(scene_output, &scene->outputs, link) {
			wlr_surface_send_leave(scene_surface->surface, scene_output->output);
		}

		wl_list_remove(&scene_surface->surface_destroy.link);
		free(scene_surface);
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = scene_rect_from_node(node);
		free(scene_rect);
		break;
	}
}

struct wlr_scene *wlr_scene_create(void) {
	struct wlr_scene *scene = calloc(1, sizeof(struct wlr_scene));
	if (scene == NULL) {
		return NULL;
	}
	scene_node_init(&scene->node, WLR_SCENE_NODE_ROOT, NULL);
	wl_list_init(&scene->outputs);
	return scene;
}

static void scene_surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_surface *scene_surface =
		wl_container_of(listener, scene_surface, surface_destroy);
	wlr_scene_node_destroy(&scene_surface->node);
}

static struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node) {
	while (node->parent != NULL) {
		node = node->parent;
	}
	return scene_root_from_node(node);
}

static void surface_update_outputs(struct wlr_surface *surface,
		struct wlr_scene *scene, int lx, int ly, bool enabled) {
	struct wlr_box surface_box = {0};
	if (enabled) {
		surface_box.x = lx;
		surface_box.y = ly;
		surface_box.width = surface->current.width;
		surface_box.height = surface->current.height;
	}

	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		struct wlr_box output_box = {
			.x = scene_output->x,
			.y = scene_output->y,
		};
		wlr_output_effective_resolution(scene_output->output,
			&output_box.width, &output_box.height);

		struct wlr_box intersection;
		if (wlr_box_intersection(&intersection, &surface_box, &output_box)) {
			wlr_surface_send_enter(surface, scene_output->output);
		} else {
			wlr_surface_send_leave(surface, scene_output->output);
		}
	}
}

static void _scene_node_update_surface_outputs(struct wlr_scene_node *node,
		struct wlr_scene *scene, int lx, int ly, bool enabled) {
	struct wlr_scene_node *child;
	wl_list_for_each(child, &node->state.children, state.link) {
		_scene_node_update_surface_outputs(node, scene,
			lx + child->state.x, ly + child->state.y,
			enabled && child->state.enabled);
	}

	if (node->type != WLR_SCENE_NODE_SURFACE) {
		return;
	}

	struct wlr_scene_surface *scene_surface = wlr_scene_surface_from_node(node);
	surface_update_outputs(scene_surface->surface, scene, lx, ly, enabled);
}

static void scene_node_update_surface_outputs(struct wlr_scene_node *node) {
	struct wlr_scene *scene = scene_node_get_root(node);

	int lx, ly;
	wlr_scene_node_coords(node, &lx, &ly);

	_scene_node_update_surface_outputs(node, scene, lx, ly, node->state.enabled);
}

static void scene_surface_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_surface *scene_surface =
		wl_container_of(listener, scene_surface, surface_commit);
	struct wlr_surface *surface = scene_surface->surface;

	if (!pixman_region32_not_empty(&surface->buffer_damage)) {
		return;
	}

	int lx, ly;
	if (!wlr_scene_node_coords(&scene_surface->node, &lx, &ly)) {
		return;
	}

	struct wlr_scene *scene = scene_node_get_root(&scene_surface->node);

	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		struct wlr_output *output = scene_output->output;

		pixman_region32_t damage;
		pixman_region32_init(&damage);
		wlr_surface_get_effective_damage(surface, &damage);

		pixman_region32_translate(&damage,
			lx - scene_output->x, ly - scene_output->y);

		wlr_region_scale(&damage, &damage, output->scale);
		if (ceil(output->scale) > surface->current.scale) {
			// When scaling up a surface it'll become blurry, so we need to
			// expand the damage region.
			wlr_region_expand(&damage, &damage,
				ceil(output->scale) - surface->current.scale);
		}
		wlr_output_damage_add(scene_output->damage, &damage);
		pixman_region32_fini(&damage);
	}

	if (surface->current.width != scene_surface->prev_width ||
			surface->current.height != scene_surface->prev_height) {
		// TODO: figure out if any parent is disabled
		surface_update_outputs(scene_surface->surface, scene, lx, ly,
			scene_surface->node.state.enabled);
		scene_surface->prev_width = surface->current.width;
		scene_surface->prev_height = surface->current.height;
	}
}

struct wlr_scene_surface *wlr_scene_surface_create(struct wlr_scene_node *parent,
		struct wlr_surface *surface) {
	struct wlr_scene_surface *scene_surface =
		calloc(1, sizeof(struct wlr_scene_surface));
	if (scene_surface == NULL) {
		return NULL;
	}
	scene_node_init(&scene_surface->node, WLR_SCENE_NODE_SURFACE, parent);

	scene_surface->surface = surface;

	scene_surface->surface_destroy.notify = scene_surface_handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &scene_surface->surface_destroy);

	scene_surface->surface_commit.notify = scene_surface_handle_surface_commit;
	wl_signal_add(&surface->events.commit, &scene_surface->surface_commit);

	scene_node_damage_whole(&scene_surface->node);
	scene_node_update_surface_outputs(&scene_surface->node);

	return scene_surface;
}

struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_node *parent,
		int width, int height, const float color[static 4]) {
	struct wlr_scene_rect *scene_rect =
		calloc(1, sizeof(struct wlr_scene_rect));
	if (scene_rect == NULL) {
		return NULL;
	}
	scene_node_init(&scene_rect->node, WLR_SCENE_NODE_RECT, parent);

	scene_rect->width = width;
	scene_rect->height = height;
	memcpy(scene_rect->color, color, sizeof(scene_rect->color));

	scene_node_damage_whole(&scene_rect->node);

	return scene_rect;
}

void wlr_scene_rect_set_size(struct wlr_scene_rect *rect, int width, int height) {
	if (rect->width == width && rect->height == height) {
		return;
	}

	scene_node_damage_whole(&rect->node);
	rect->width = width;
	rect->height = height;
	scene_node_damage_whole(&rect->node);
}

void wlr_scene_rect_set_color(struct wlr_scene_rect *rect, const float color[static 4]) {
	if (memcmp(rect->color, color, sizeof(rect->color)) == 0) {
		return;
	}

	memcpy(rect->color, color, sizeof(rect->color));
	scene_node_damage_whole(&rect->node);
}

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

static void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

static void _scene_node_damage_whole(struct wlr_scene_node *node,
		struct wlr_scene *scene, int lx, int ly) {
	if (!node->state.enabled) {
		return;
	}

	struct wlr_scene_node *child;
	wl_list_for_each(child, &node->state.children, state.link) {
		_scene_node_damage_whole(child, scene,
			lx + child->state.x, ly + child->state.y);
	}

	int width = 0, height = 0;
	switch (node->type) {
	case WLR_SCENE_NODE_ROOT:
		return;
	case WLR_SCENE_NODE_SURFACE:;
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_from_node(node);
		width = scene_surface->surface->current.width;
		height = scene_surface->surface->current.height;
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = scene_rect_from_node(node);
		width = scene_rect->width;
		height = scene_rect->height;
		break;
	}

	struct wlr_scene_output *scene_output;
	wl_list_for_each(scene_output, &scene->outputs, link) {
		struct wlr_box box = {
			.x = lx - scene_output->x,
			.y = ly - scene_output->y,
			.width = width,
			.height = height,
		};

		scale_box(&box, scene_output->output->scale);

		wlr_output_damage_add_box(scene_output->damage, &box);
	}
}

static void scene_node_damage_whole(struct wlr_scene_node *node) {
	struct wlr_scene *scene = scene_node_get_root(node);
	if (wl_list_empty(&scene->outputs)) {
		return;
	}

	int lx, ly;
	if (!wlr_scene_node_coords(node, &lx, &ly)) {
		return;
	}

	_scene_node_damage_whole(node, scene, lx, ly);
}

void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled) {
	if (node->state.enabled == enabled) {
		return;
	}

	// One of these damage_whole() calls will short-circuit and be a no-op
	scene_node_damage_whole(node);
	node->state.enabled = enabled;
	scene_node_damage_whole(node);
	scene_node_update_surface_outputs(node);
}

void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y) {
	if (node->state.x == x && node->state.y == y) {
		return;
	}

	scene_node_damage_whole(node);
	node->state.x = x;
	node->state.y = y;
	scene_node_damage_whole(node);
	scene_node_update_surface_outputs(node);
}

void wlr_scene_node_place_above(struct wlr_scene_node *node,
		struct wlr_scene_node *sibling) {
	assert(node->parent == sibling->parent);

	if (node->state.link.prev == &sibling->state.link) {
		return;
	}

	wl_list_remove(&node->state.link);
	wl_list_insert(&sibling->state.link, &node->state.link);

	scene_node_damage_whole(node);
	scene_node_damage_whole(sibling);
}

void wlr_scene_node_place_below(struct wlr_scene_node *node,
		struct wlr_scene_node *sibling) {
	assert(node->parent == sibling->parent);

	if (node->state.link.next == &sibling->state.link) {
		return;
	}

	wl_list_remove(&node->state.link);
	wl_list_insert(sibling->state.link.prev, &node->state.link);

	scene_node_damage_whole(node);
	scene_node_damage_whole(sibling);
}

void wlr_scene_node_reparent(struct wlr_scene_node *node,
		struct wlr_scene_node *new_parent) {
	assert(node->type != WLR_SCENE_NODE_ROOT && new_parent != NULL);

	if (node->parent == new_parent) {
		return;
	}

	/* Ensure that a node cannot become its own ancestor */
	for (struct wlr_scene_node *ancestor = new_parent; ancestor != NULL;
			ancestor = ancestor->parent) {
		assert(ancestor != node);
	}

	scene_node_damage_whole(node);

	wl_list_remove(&node->state.link);
	node->parent = new_parent;
	wl_list_insert(new_parent->state.children.prev, &node->state.link);

	scene_node_damage_whole(node);
}

bool wlr_scene_node_coords(struct wlr_scene_node *node,
		int *lx_ptr, int *ly_ptr) {
	int lx = 0, ly = 0;
	bool enabled = true;
	while (node != NULL) {
		lx += node->state.x;
		ly += node->state.y;
		enabled = enabled && node->state.enabled;
		node = node->parent;
	}

	*lx_ptr = lx;
	*ly_ptr = ly;
	return enabled;
}

static void scene_node_for_each_surface(struct wlr_scene_node *node,
		int lx, int ly, wlr_surface_iterator_func_t user_iterator,
		void *user_data) {
	if (!node->state.enabled) {
		return;
	}

	lx += node->state.x;
	ly += node->state.y;

	if (node->type == WLR_SCENE_NODE_SURFACE) {
		struct wlr_scene_surface *scene_surface = wlr_scene_surface_from_node(node);
		user_iterator(scene_surface->surface, lx, ly, user_data);
	}

	struct wlr_scene_node *child;
	wl_list_for_each(child, &node->state.children, state.link) {
		scene_node_for_each_surface(child, lx, ly, user_iterator, user_data);
	}
}

void wlr_scene_node_for_each_surface(struct wlr_scene_node *node,
		wlr_surface_iterator_func_t user_iterator, void *user_data) {
	scene_node_for_each_surface(node, 0, 0, user_iterator, user_data);
}

struct wlr_scene_node *wlr_scene_node_at(struct wlr_scene_node *node,
		double lx, double ly, double *nx, double *ny) {
	if (!node->state.enabled) {
		return NULL;
	}

	// TODO: optimize by storing a bounding box in each node?
	lx -= node->state.x;
	ly -= node->state.y;

	struct wlr_scene_node *child;
	wl_list_for_each_reverse(child, &node->state.children, state.link) {
		struct wlr_scene_node *node =
			wlr_scene_node_at(child, lx, ly, nx, ny);
		if (node != NULL) {
			return node;
		}
	}

	switch (node->type) {
	case WLR_SCENE_NODE_SURFACE:;
		struct wlr_scene_surface *scene_surface = wlr_scene_surface_from_node(node);
		if (wlr_surface_point_accepts_input(scene_surface->surface, lx, ly)) {
			if (nx != NULL) {
				*nx = lx;
			}
			if (ny != NULL) {
				*ny = ly;
			}
			return &scene_surface->node;
		}
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *rect = scene_rect_from_node(node);
		if (lx >= 0 && lx < rect->width && ly >= 0 && ly < rect->height) {
			if (nx != NULL) {
				*nx = lx;
			}
			if (ny != NULL) {
				*ny = ly;
			}
			return &rect->node;
		}
		break;
	default:
		break;
	}

	return NULL;
}

static void scissor_output(struct wlr_output *output, pixman_box32_t *rect) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};

	int ow, oh;
	wlr_output_transformed_resolution(output, &ow, &oh);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);
	wlr_box_transform(&box, &box, transform, ow, oh);

	wlr_renderer_scissor(renderer, &box);
}

static void render_rect(struct wlr_output *output,
		pixman_region32_t *output_damage, const float color[static 4],
		const struct wlr_box *box, const float matrix[static 9]) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_init_rect(&damage, box->x, box->y, box->width, box->height);
	pixman_region32_intersect(&damage, &damage, output_damage);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_render_rect(renderer, box, color, matrix);
	}

	pixman_region32_fini(&damage);
}

static void render_texture(struct wlr_output *output,
		pixman_region32_t *output_damage, struct wlr_texture *texture,
		const struct wlr_box *box, const float matrix[static 9]) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_init_rect(&damage, box->x, box->y, box->width, box->height);
	pixman_region32_intersect(&damage, &damage, output_damage);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_render_texture_with_matrix(renderer, texture, matrix, 1.0);
	}

	pixman_region32_fini(&damage);
}

struct render_data {
	struct wlr_output *output;
	pixman_region32_t *damage;
};

static void render_node_iterator(struct wlr_scene_node *node,
		int x, int y, void *_data) {
	struct render_data *data = _data;
	struct wlr_output *output = data->output;
	pixman_region32_t *output_damage = data->damage;
	struct wlr_box box = {
		.x = x,
		.y = y,
	};

	switch (node->type) {
	case WLR_SCENE_NODE_ROOT:;
		/* Root node has nothing to render itself */
		break;
	case WLR_SCENE_NODE_SURFACE:;
		struct wlr_scene_surface *scene_surface = wlr_scene_surface_from_node(node);
		struct wlr_surface *surface = scene_surface->surface;

		struct wlr_texture *texture = wlr_surface_get_texture(surface);
		if (texture == NULL) {
			return;
		}

		box.width = surface->current.width;
		box.height = surface->current.height;
		scale_box(&box, output->scale);

		float matrix[9];
		enum wl_output_transform transform =
			wlr_output_transform_invert(surface->current.transform);
		wlr_matrix_project_box(matrix, &box, transform, 0.0,
			output->transform_matrix);

		render_texture(output, output_damage, texture, &box, matrix);
		break;
	case WLR_SCENE_NODE_RECT:;
		struct wlr_scene_rect *scene_rect = scene_rect_from_node(node);

		box.width = scene_rect->width;
		box.height = scene_rect->height;
		scale_box(&box, data->output->scale);

		render_rect(output, output_damage, scene_rect->color, &box,
				output->transform_matrix);
		break;
	}
}

static void scene_node_for_each_node(struct wlr_scene_node *node,
		int lx, int ly, wlr_scene_node_iterator_func_t user_iterator,
		void *user_data) {
	if (!node->state.enabled) {
		return;
	}

	lx += node->state.x;
	ly += node->state.y;

	user_iterator(node, lx, ly, user_data);

	struct wlr_scene_node *child;
	wl_list_for_each(child, &node->state.children, state.link) {
		scene_node_for_each_node(child, lx, ly, user_iterator, user_data);
	}
}

void wlr_scene_render_output(struct wlr_scene *scene, struct wlr_output *output,
		int lx, int ly, pixman_region32_t *damage) {
	pixman_region32_t full_region;
	pixman_region32_init_rect(&full_region, 0, 0, output->width, output->height);
	if (damage == NULL) {
		damage = &full_region;
	}

	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(output->backend);
	assert(renderer);

	if (output->enabled && pixman_region32_not_empty(damage)) {
		struct render_data data = {
			.output = output,
			.damage = damage,
		};
		scene_node_for_each_node(&scene->node, -lx, -ly,
			render_node_iterator, &data);
		wlr_renderer_scissor(renderer, NULL);
	}

	pixman_region32_fini(&full_region);
}

static void scene_output_handle_destroy(struct wlr_addon *addon) {
	struct wlr_scene_output *scene_output =
		wl_container_of(addon, scene_output, addon);
	wlr_scene_output_destroy(scene_output);
}

static const struct wlr_addon_interface output_addon_impl = {
	.name = "wlr_scene_output",
	.destroy = scene_output_handle_destroy,
};

struct wlr_scene_output *wlr_scene_output_create(struct wlr_scene *scene,
		struct wlr_output *output) {
	struct wlr_scene_output *scene_output = calloc(1, sizeof(*output));
	if (scene_output == NULL) {
		return NULL;
	}

	scene_output->damage = wlr_output_damage_create(output);
	if (scene_output->damage == NULL) {
		free(scene_output);
		return NULL;
	}

	scene_output->output = output;
	scene_output->scene = scene;
	wlr_addon_init(&scene_output->addon, &output->addons, scene, &output_addon_impl);
	wl_list_insert(&scene->outputs, &scene_output->link);

	wlr_output_damage_add_whole(scene_output->damage);
	scene_node_update_surface_outputs(&scene->node);

	return scene_output;
}

void wlr_scene_output_destroy(struct wlr_scene_output *scene_output) {
	// TODO: consider leaving all surfaces
	wlr_addon_finish(&scene_output->addon);
	wl_list_remove(&scene_output->link);
	free(scene_output);
}

void wlr_scene_output_set_position(struct wlr_scene_output *scene_output,
		int lx, int ly) {
	if (scene_output->x == lx && scene_output->y == ly) {
		return;
	}

	scene_output->x = lx;
	scene_output->y = ly;
	wlr_output_damage_add_whole(scene_output->damage);
	scene_node_update_surface_outputs(&scene_output->scene->node);
}

bool wlr_scene_output_commit(struct wlr_scene_output *scene_output) {
	struct wlr_output *output = scene_output->output;

	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer != NULL);

	bool needs_frame;
	pixman_region32_t damage;
	pixman_region32_init(&damage);
	if (!wlr_output_damage_attach_render(scene_output->damage,
			&needs_frame, &damage)) {
		pixman_region32_fini(&damage);
		return false;
	}

	if (!needs_frame) {
		pixman_region32_fini(&damage);
		wlr_output_rollback(output);
		return true;
	}

	wlr_renderer_begin(renderer, output->width, output->height);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_renderer_clear(renderer, (float[4]){ 0.0, 0.0, 0.0, 1.0 });
	}

	wlr_scene_render_output(scene_output->scene, output,
		scene_output->x, scene_output->y, &damage);
	wlr_output_render_software_cursors(output, &damage);

	wlr_renderer_end(renderer);
	pixman_region32_fini(&damage);

	int tr_width, tr_height;
	wlr_output_transformed_resolution(output, &tr_width, &tr_height);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);

	pixman_region32_t frame_damage;
	pixman_region32_init(&frame_damage);
	wlr_region_transform(&frame_damage, &scene_output->damage->current,
		transform, tr_width, tr_height);
	wlr_output_set_damage(output, &frame_damage);
	pixman_region32_fini(&frame_damage);

	return wlr_output_commit(output);
}
