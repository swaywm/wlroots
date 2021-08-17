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
 * compositor creates a scene, adds surfaces, then renders the scene on
 * outputs.
 *
 * The scene-graph API only supports basic 2D composition operations (like the
 * KMS API or the Wayland protocol does). For anything more complicated,
 * compositors need to implement custom rendering logic.
 */

#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_surface.h>

struct wlr_output;

enum wlr_scene_node_type {
	WLR_SCENE_NODE_ROOT,
	WLR_SCENE_NODE_SURFACE,
	WLR_SCENE_NODE_RECT,
};

struct wlr_scene_node_state {
	struct wl_list link; // wlr_scene_node_state.children

	struct wl_list children; // wlr_scene_node_state.link

	bool enabled;
	int x, y; // relative to parent
};

/** A node is an object in the scene. */
struct wlr_scene_node {
	enum wlr_scene_node_type type;
	struct wlr_scene_node *parent;
	struct wlr_scene_node_state state;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

/** The root scene-graph node. */
struct wlr_scene {
	struct wlr_scene_node node;

	struct wl_list outputs; // wlr_scene_output.link
};

/** A scene-graph node displaying a single surface. */
struct wlr_scene_surface {
	struct wlr_scene_node node;
	struct wlr_surface *surface;

	// private state

	struct wl_listener surface_destroy;
};

/** A scene-graph node displaying a solid-colored rectangle */
struct wlr_scene_rect {
	struct wlr_scene_node node;
	int width, height;
	float color[4];
};

/** A viewport for an output in the scene-graph */
struct wlr_scene_output {
	struct wlr_output *output;
	struct wl_list link; // wlr_scene.outputs
	struct wlr_scene *scene;
	struct wlr_addon addon;

	int x, y;
};

typedef void (*wlr_scene_node_iterator_func_t)(struct wlr_scene_node *node,
	int sx, int sy, void *data);

/**
 * Immediately destroy the scene-graph node.
 */
void wlr_scene_node_destroy(struct wlr_scene_node *node);
/**
 * Enable or disable this node. If a node is disabled, all of its children are
 * implicitly disabled as well.
 */
void wlr_scene_node_set_enabled(struct wlr_scene_node *node, bool enabled);
/**
 * Set the position of the node relative to its parent.
 */
void wlr_scene_node_set_position(struct wlr_scene_node *node, int x, int y);
/**
 * Move the node right above the specified sibling.
 */
void wlr_scene_node_place_above(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
/**
 * Move the node right below the specified sibling.
 */
void wlr_scene_node_place_below(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
/**
 * Move the node to another location in the tree.
 */
void wlr_scene_node_reparent(struct wlr_scene_node *node,
	struct wlr_scene_node *new_parent);
/**
 * Call `iterator` on each surface in the scene-graph, with the surface's
 * position in layout coordinates. The function is called from root to leaves
 * (in rendering order).
 */
void wlr_scene_node_for_each_surface(struct wlr_scene_node *node,
	wlr_surface_iterator_func_t iterator, void *user_data);
/**
 * Find the topmost node in this scene-graph that contains the point at the
 * given layout-local coordinates. (For surface nodes, this means accepting
 * input events at that point.) Returns the node and coordinates relative to the
 * returned node, or NULL if no node is found at that location.
 */
struct wlr_scene_node *wlr_scene_node_at(struct wlr_scene_node *node,
	double lx, double ly, double *nx, double *ny);

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
void wlr_scene_render_output(struct wlr_scene *scene, struct wlr_output *output,
	int lx, int ly, pixman_region32_t *damage);

/**
 * Add a node displaying a single surface to the scene-graph.
 *
 * The child sub-surfaces are ignored.
 */
struct wlr_scene_surface *wlr_scene_surface_create(struct wlr_scene_node *parent,
	struct wlr_surface *surface);

struct wlr_scene_surface *wlr_scene_surface_from_node(struct wlr_scene_node *node);

/**
 * Add a node displaying a solid-colored rectangle to the scene-graph.
 */
struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_node *parent,
		int width, int height, const float color[static 4]);

/**
 * Change the width and height of an existing rectangle node.
 */
void wlr_scene_rect_set_size(struct wlr_scene_rect *rect, int width, int height);

/**
 * Change the color of an existing rectangle node.
 */
void wlr_scene_rect_set_color(struct wlr_scene_rect *rect, const float color[static 4]);

/**
 * Add a viewport for the specified output to the scene-graph.
 *
 * An output can only be added once to the scene-graph.
 */
struct wlr_scene_output *wlr_scene_output_create(struct wlr_scene *scene,
	struct wlr_output *output);
/**
 * Destroy a scene-graph output.
 */
void wlr_scene_output_destroy(struct wlr_scene_output *scene_output);
/**
 * Set the output's position in the scene-graph.
 */
void wlr_scene_output_set_position(struct wlr_scene_output *scene_output,
	int lx, int ly);
/**
 * Render and commit an output.
 */
bool wlr_scene_output_commit(struct wlr_scene_output *scene_output);

#endif
