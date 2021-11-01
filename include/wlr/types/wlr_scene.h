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
struct wlr_output_layout;
struct wlr_xdg_popup;

enum wlr_scene_node_type {
	WLR_SCENE_NODE_ROOT,
	WLR_SCENE_NODE_TREE,
	WLR_SCENE_NODE_SURFACE,
	WLR_SCENE_NODE_RECT,
	WLR_SCENE_NODE_BUFFER,
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

/** A sub-tree in the scene-graph. */
struct wlr_scene_tree {
	struct wlr_scene_node node;
};

/** A scene-graph node displaying a single surface. */
struct wlr_scene_surface {
	struct wlr_scene_node node;
	struct wlr_surface *surface;

	// private state

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;
};

/** A scene-graph node displaying a solid-colored rectangle */
struct wlr_scene_rect {
	struct wlr_scene_node node;
	int width, height;
	float color[4];
};

/** A scene-graph node displaying a buffer */
struct wlr_scene_buffer {
	struct wlr_scene_node node;
	struct wlr_buffer *buffer;

	// private state

	struct wlr_texture *texture;
	struct wlr_fbox src_box;
	int dst_width, dst_height;
	enum wl_output_transform transform;
};

/** A viewport for an output in the scene-graph */
struct wlr_scene_output {
	struct wlr_output *output;
	struct wl_list link; // wlr_scene.outputs
	struct wlr_scene *scene;
	struct wlr_addon addon;

	struct wlr_output_damage *damage;

	int x, y;

	// private state

	bool prev_scanout;
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
 * Asserts that node and sibling are distinct and share the same parent.
 */
void wlr_scene_node_place_above(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
/**
 * Move the node right below the specified sibling.
 * Asserts that node and sibling are distinct and share the same parent.
 */
void wlr_scene_node_place_below(struct wlr_scene_node *node,
	struct wlr_scene_node *sibling);
/**
 * Move the node above all of its sibling nodes.
 */
void wlr_scene_node_raise_to_top(struct wlr_scene_node *node);
/**
 * Move the node below all of its sibling nodes.
 */
void wlr_scene_node_lower_to_bottom(struct wlr_scene_node *node);
/**
 * Move the node to another location in the tree.
 */
void wlr_scene_node_reparent(struct wlr_scene_node *node,
	struct wlr_scene_node *new_parent);
/**
 * Get the node's layout-local coordinates.
 *
 * True is returned if the node and all of its ancestors are enabled.
 */
bool wlr_scene_node_coords(struct wlr_scene_node *node, int *lx, int *ly);
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
 * Add a node displaying nothing but its children.
 */
struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_node *parent);

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
 * Add a node displaying a buffer to the scene-graph.
 */
struct wlr_scene_buffer *wlr_scene_buffer_create(struct wlr_scene_node *parent,
	struct wlr_buffer *buffer);

/**
 * Set the source rectangle describing the region of the buffer which will be
 * sampled to render this node. This allows cropping the buffer.
 *
 * If NULL, the whole buffer is sampled. By default, the source box is NULL.
 */
void wlr_scene_buffer_set_source_box(struct wlr_scene_buffer *scene_buffer,
	const struct wlr_fbox *box);

/**
 * Set the destination size describing the region of the scene-graph the buffer
 * will be painted onto. This allows scaling the buffer.
 *
 * If zero, the destination size will be the buffer size. By default, the
 * destination size is zero.
 */
void wlr_scene_buffer_set_dest_size(struct wlr_scene_buffer *scene_buffer,
	int width, int height);

/**
 * Set a transform which will be applied to the buffer.
 */
void wlr_scene_buffer_set_transform(struct wlr_scene_buffer *scene_buffer,
	enum wl_output_transform transform);

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

/**
 * Call `iterator` on each surface in the scene-graph visible on the output,
 * with the surface's position in layout coordinates. The function is called
 * from root to leaves (in rendering order).
 */
void wlr_scene_output_for_each_surface(struct wlr_scene_output *scene_output,
	wlr_surface_iterator_func_t iterator, void *user_data);

/**
 * Attach an output layout to a scene.
 *
 * Outputs in the output layout are automatically added to the scene. Any
 * change to the output layout is mirrored to the scene-graph outputs.
 */
bool wlr_scene_attach_output_layout(struct wlr_scene *scene,
	struct wlr_output_layout *output_layout);

/**
 * Add a node displaying a surface and all of its sub-surfaces to the
 * scene-graph.
 */
struct wlr_scene_node *wlr_scene_subsurface_tree_create(
	struct wlr_scene_node *parent, struct wlr_surface *surface);

/**
 * Add a node displaying an xdg_popup and all of its sub-surfaces to the
 * scene-graph.
 */
struct wlr_scene_node *wlr_scene_xdg_popup_create(
	struct wlr_scene_node *parent, struct wlr_xdg_popup *popup);

#endif
