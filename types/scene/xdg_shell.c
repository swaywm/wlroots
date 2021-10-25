#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

struct wlr_scene_xdg_popup {
	struct wlr_scene_tree *tree;
	struct wlr_xdg_popup *popup;
	struct wlr_scene_node *surface_node;

	struct wl_listener tree_destroy;
	struct wl_listener popup_destroy;
	struct wl_listener popup_map;
	struct wl_listener popup_unmap;
	struct wl_listener popup_ack_configure;
};

static void scene_popup_handle_tree_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_xdg_popup *scene_popup =
		wl_container_of(listener, scene_popup, tree_destroy);
	// tree and surface_node will be cleaned up by scene_node_finish
	wl_list_remove(&scene_popup->tree_destroy.link);
	wl_list_remove(&scene_popup->popup_destroy.link);
	wl_list_remove(&scene_popup->popup_map.link);
	wl_list_remove(&scene_popup->popup_unmap.link);
	wl_list_remove(&scene_popup->popup_ack_configure.link);
	free(scene_popup);
}

static void scene_popup_handle_popup_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_xdg_popup *scene_popup =
		wl_container_of(listener, scene_popup, popup_destroy);
	wlr_scene_node_destroy(&scene_popup->tree->node);
}

static void scene_popup_handle_popup_map(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_xdg_popup *scene_popup =
		wl_container_of(listener, scene_popup, popup_map);
	wlr_scene_node_set_enabled(&scene_popup->tree->node, true);
}

static void scene_popup_handle_popup_unmap(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_xdg_popup *scene_popup =
		wl_container_of(listener, scene_popup, popup_unmap);
	wlr_scene_node_set_enabled(&scene_popup->tree->node, false);
}

static void scene_popup_update_position(
		struct wlr_scene_xdg_popup *scene_popup) {
	struct wlr_xdg_popup *popup = scene_popup->popup;

	struct wlr_box geo = {0};
	wlr_xdg_surface_get_geometry(popup->base, &geo);

	wlr_scene_node_set_position(&scene_popup->tree->node,
		popup->geometry.x - geo.x, popup->geometry.y - geo.y);
}

static void scene_popup_handle_popup_ack_configure(struct wl_listener *listener,
		void *data) {
	struct wlr_scene_xdg_popup *scene_popup =
		wl_container_of(listener, scene_popup, popup_ack_configure);
	scene_popup_update_position(scene_popup);
}

struct wlr_scene_node *wlr_scene_xdg_popup_create(
		struct wlr_scene_node *parent, struct wlr_xdg_popup *popup) {
	struct wlr_scene_xdg_popup *scene_popup = calloc(1, sizeof(*scene_popup));
	if (scene_popup == NULL) {
		return NULL;
	}

	scene_popup->popup = popup;

	scene_popup->tree = wlr_scene_tree_create(parent);
	if (scene_popup->tree == NULL) {
		free(scene_popup);
		return NULL;
	}

	scene_popup->surface_node = wlr_scene_subsurface_tree_create(
		&scene_popup->tree->node, popup->base->surface);
	if (scene_popup->surface_node == NULL) {
		wlr_scene_node_destroy(&scene_popup->tree->node);
		free(scene_popup);
		return NULL;
	}

	scene_popup->tree_destroy.notify = scene_popup_handle_tree_destroy;
	wl_signal_add(&scene_popup->tree->node.events.destroy,
		&scene_popup->tree_destroy);

	scene_popup->popup_destroy.notify = scene_popup_handle_popup_destroy;
	wl_signal_add(&popup->base->events.destroy, &scene_popup->popup_destroy);

	scene_popup->popup_map.notify = scene_popup_handle_popup_map;
	wl_signal_add(&popup->base->events.map, &scene_popup->popup_map);

	scene_popup->popup_unmap.notify = scene_popup_handle_popup_unmap;
	wl_signal_add(&popup->base->events.unmap, &scene_popup->popup_unmap);

	scene_popup->popup_ack_configure.notify =
		scene_popup_handle_popup_ack_configure;
	wl_signal_add(&popup->base->events.ack_configure,
		&scene_popup->popup_ack_configure);

	wlr_scene_node_set_enabled(&scene_popup->tree->node, popup->base->mapped);
	scene_popup_update_position(scene_popup);

	return &scene_popup->tree->node;
}
