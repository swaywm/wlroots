#include <assert.h>
#include <stdlib.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_surface.h>
#include "util/signal.h"

static struct wlr_scene *scene_root_from_node(struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_ROOT);
	return (struct wlr_scene *)node;
}

static struct wlr_scene_surface *scene_surface_from_node(
		struct wlr_scene_node *node) {
	assert(node->type == WLR_SCENE_NODE_SURFACE);
	return (struct wlr_scene_surface *)node;
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

void wlr_scene_node_destroy(struct wlr_scene_node *node) {
	if (node == NULL) {
		return;
	}

	scene_node_finish(node);

	switch (node->type) {
	case WLR_SCENE_NODE_ROOT:;
		struct wlr_scene *scene = scene_root_from_node(node);
		free(scene);
		break;
	case WLR_SCENE_NODE_SURFACE:;
		struct wlr_scene_surface *scene_surface = scene_surface_from_node(node);
		wl_list_remove(&scene_surface->surface_destroy.link);
		free(scene_surface);
		break;
	}
}

struct wlr_scene *wlr_scene_create(void) {
	struct wlr_scene *scene = calloc(1, sizeof(struct wlr_scene));
	if (scene == NULL) {
		return NULL;
	}
	scene_node_init(&scene->node, WLR_SCENE_NODE_ROOT, NULL);

	return scene;
}

static void scene_surface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_surface *scene_surface =
		wl_container_of(listener, scene_surface, surface_destroy);
	wlr_scene_node_destroy(&scene_surface->node);
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

	return scene_surface;
}

void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled) {
	node->state.enabled = enabled;
}

void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y) {
	node->state.x = x;
	node->state.y = y;
}

void wlr_scene_node_place_above(struct wlr_scene_node *node,
		struct wlr_scene_node *sibling) {
	assert(node->parent == sibling->parent);

	wl_list_remove(&node->state.link);
	wl_list_insert(&sibling->state.link, &node->state.link);
}

void wlr_scene_node_place_below(struct wlr_scene_node *node,
		struct wlr_scene_node *sibling) {
	assert(node->parent == sibling->parent);

	wl_list_remove(&node->state.link);
	wl_list_insert(sibling->state.link.prev, &node->state.link);
}

void wlr_scene_node_reparent(struct wlr_scene_node *node,
		struct wlr_scene_node *new_parent) {
	if (node->parent == new_parent) {
		return;
	}
	/* Ensure that a node cannot become its own ancestor */
	for (struct wlr_scene_node *ancestor = new_parent; ancestor != NULL;
			ancestor = ancestor->parent) {
		assert(ancestor != node);
	}

	wl_list_remove(&node->state.link);

	node->parent = new_parent;
	if (new_parent != NULL) {
		wl_list_insert(new_parent->state.children.prev, &node->state.link);
	}
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
		struct wlr_scene_surface *scene_surface = scene_surface_from_node(node);
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

struct wlr_surface *wlr_scene_node_surface_at(struct wlr_scene_node *node,
		double lx, double ly, double *sx, double *sy) {
	if (!node->state.enabled) {
		return NULL;
	}

	// TODO: optimize by storing a bounding box in each node?
	lx -= node->state.x;
	ly -= node->state.y;

	struct wlr_scene_node *child;
	wl_list_for_each_reverse(child, &node->state.children, state.link) {
		struct wlr_surface *surface =
			wlr_scene_node_surface_at(child, lx, ly, sx, sy);
		if (surface != NULL) {
			return surface;
		}
	}

	if (node->type == WLR_SCENE_NODE_SURFACE) {
		struct wlr_scene_surface *scene_surface = scene_surface_from_node(node);
		if (wlr_surface_point_accepts_input(scene_surface->surface, lx, ly)) {
			if (sx != NULL) {
				*sx = lx;
			}
			if (sy != NULL) {
				*sy = ly;
			}
			return scene_surface->surface;
		}
	}

	return NULL;
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

static void render_texture(struct wlr_output *output,
		pixman_region32_t *output_damage, struct wlr_texture *texture,
		const struct wlr_box *box, const float matrix[static 9]) {
	struct wlr_renderer *renderer = wlr_backend_get_renderer(output->backend);
	assert(renderer);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_init_rect(&damage, box->x, box->y, box->width, box->height);
	pixman_region32_intersect(&damage, &damage, output_damage);
	if (pixman_region32_not_empty(&damage)) {
		int nrects;
		pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
		for (int i = 0; i < nrects; ++i) {
			scissor_output(output, &rects[i]);
			wlr_render_texture_with_matrix(renderer, texture, matrix, 1.0);
		}
	}
	pixman_region32_fini(&damage);
}

struct render_data {
	struct wlr_output *output;
	pixman_region32_t *damage;
};

static void render_surface_iterator(struct wlr_surface *surface,
		int x, int y, void *_data) {
	struct render_data *data = _data;
	struct wlr_output *output = data->output;
	pixman_region32_t *output_damage = data->damage;

	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL) {
		return;
	}

	struct wlr_box box = {
		.x = x,
		.y = y,
		.width = surface->current.width,
		.height = surface->current.height,
	};
	scale_box(&box, output->scale);

	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, 0.0,
		output->transform_matrix);

	render_texture(output, output_damage, texture, &box, matrix);
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
		scene_node_for_each_surface(&scene->node, lx, ly,
			render_surface_iterator, &data);
		wlr_renderer_scissor(renderer, NULL);
	}

	pixman_region32_fini(&full_region);
}
