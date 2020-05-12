/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_SCENE_H
#define WLR_TYPES_WLR_SCENE_H

/**
 * The scene-graph API provides a declarative way to display surfaces. The
 * compositor creates a scene, adds surfaces, then renders the whole scene.
 *
 * The API is atomic: changes are not applied until a commit.
 */

#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_surface.h>

struct wlr_output;

enum wlr_scene_node_type {
	WLR_SCENE_NODE_ROOT,
	WLR_SCENE_NODE_SURFACE,
};

struct wlr_scene_node_state {
	struct wl_list link; // wlr_scene_node_state.children

	struct wl_list children; // wlr_scene_node_state.link

	bool enabled;
	int x, y;
};

struct wlr_scene_node {
	enum wlr_scene_node_type type;
	struct wlr_scene_node *parent;
	struct wlr_scene_node_state current, pending;

	struct {
		struct wl_signal destroy;
	} events;
};

/** The root scene-graph node. */
struct wlr_scene {
	struct wlr_scene_node node;
};

/** A scene-graph node displaying a surface and its subsurfaces. */
struct wlr_scene_surface {
	struct wlr_scene_node node;
	struct wlr_surface *surface;

	struct wl_list surface_outputs; // wlr_scene_surface_output.link

	struct wl_listener surface_destroy;
};

/**
 * Immediately destroy the scene-graph node.
 */
void wlr_scene_node_destroy(struct wlr_scene_node *node);
/**
 * Atomically apply pending changes for this node and all of its children.
 */
void wlr_scene_node_commit(struct wlr_scene_node *node);
/**
 * Enable or disable this node. If a node is disabled, all of its children are
 * implicitly disabled as well. This state is double-buffered, see
 * wlr_scene_node_commit.
 */
void wlr_scene_node_toggle(struct wlr_scene_node *node, bool enabled);
/**
 * Set the position of the node relative to its parent. This state is
 * double-buffered, see wlr_scene_node_commit.
 */
void wlr_scene_node_move(struct wlr_scene_node *node, int x, int y);
/**
 * Move the node right above the specified sibling. This state is
 * double-buffered, see wlr_scene_node_commit.
 */
void wlr_scene_node_place_above(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
/**
 * Move the node right below the specified sibling. This state is
 * double-buffered, see wlr_scene_node_commit.
 */
void wlr_scene_node_place_below(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
/**
 * Call `iterator` on each surface in the scene-graph, with the surface's
 * position in layout coordinates. The function is called from root to leaves
 * (in rendering order).
 */
void wlr_scene_node_for_each_surface(struct wlr_scene_node *node,
	wlr_surface_iterator_func_t iterator, void *user_data);

/**
 * Create a new scene-graph.
 */
struct wlr_scene *wlr_scene_create(void);
/**
 * Manually render the scene-graph on an output. The compositor needs to call
 * wlr_renderer_begin before and wlr_renderer_end after calling this function.
 * Damage is given in output-buffer-local coordinates and can be set to NULL to
 * disable damage tracking.
 */
void wlr_scene_render(struct wlr_scene *scene, struct wlr_output *output,
	int lx, int ly, pixman_region32_t *damage);
/**
 * Perform an output commit.
 */
bool wlr_scene_commit_output(struct wlr_scene *scene, struct wlr_output *output,
	int lx, int ly);

/**
 * Add a node displaying a surface (and its sub-surfaces) to the scene-graph.
 */
struct wlr_scene_surface *wlr_scene_surface_create(struct wlr_scene_node *parent,
	struct wlr_surface *surface);

#endif
