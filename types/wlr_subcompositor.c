#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/log.h>
#include "util/signal.h"

#include <stdio.h>

#define SUBCOMPOSITOR_VERSION 1

struct subsurface_state {
	struct wl_list link;

	struct wlr_subsurface *subsurface;
	struct wlr_commit *commit; // Does have ref count
	struct wlr_commit *parent; // Does NOT have ref count

	bool synchronized;
	bool inhibiting_parent;

	int32_t x;
	int32_t y;

	struct wl_listener subsurface_destroy;
	struct wl_listener commit_complete;
};

struct subsurface_root {
	/* This list is in stacking order.
	 * The first element is the bottom of the stack (i.e. drawn first).
	 * The last element is the top of the stack (i.e. drawn last).
	 */
	struct wl_list subsurfaces; // subsurface_state.link

	/* This represents the root surface.
	 * - It will always appear in the above subsurfaces list
	 * - subsurface will always be NULL
	 * - commit will always be NULL
	 * - (x, y) will always be (0, 0)
	 */
	struct subsurface_state root;

	struct wl_listener commit_destroy;
	struct wl_listener commit_committed;
};

static const struct wl_subsurface_interface subsurface_impl;

static struct wlr_subsurface *subsurface_from_resource(struct wl_resource *res) {
	assert(wl_resource_instance_of(res, &wl_subsurface_interface,
		&subsurface_impl));
	return wl_resource_get_user_data(res);
}

static void subsurface_destroy(struct wl_client *client, struct wl_resource *res) {
	wl_resource_destroy(res);
}

static void subsurface_set_position(struct wl_client *client,
		struct wl_resource *res, int32_t x, int32_t y) {
	//struct wlr_subsurface *ss = subsurface_from_resource(res);
	//struct wlr_commit *commit = wlr_surface_get_pending(ss->parent);
}

static void subsurface_place_above(struct wl_client *client,
		struct wl_resource *res, struct wl_resource *sibling_res) {
	struct wlr_subsurface *ss = subsurface_from_resource(res);
	if (!ss) {
		return;
	}
}

static void subsurface_place_below(struct wl_client *client,
		struct wl_resource *res, struct wl_resource *sibling_res) {
	struct wlr_subsurface *ss = subsurface_from_resource(res);
	if (!ss) {
		return;
	}
}

static void subsurface_set_sync(struct wl_client *client, struct wl_resource *res) {
	struct wlr_subsurface *ss = subsurface_from_resource(res);
	if (!ss) {
		return;
	}

	ss->synchronized = true;
}

static void subsurface_set_desync(struct wl_client *client, struct wl_resource *res) {
	struct wlr_subsurface *ss = subsurface_from_resource(res);
	if (!ss) {
		return;
	}

	ss->synchronized = false;
}

static const struct wl_subsurface_interface subsurface_impl = {
	.destroy = subsurface_destroy,
	.set_position = subsurface_set_position,
	.place_above = subsurface_place_above,
	.place_below = subsurface_place_below,
	.set_sync = subsurface_set_sync,
	.set_desync = subsurface_set_desync,
};

static void subcompositor_destroy(struct wl_client *client,
		struct wl_resource *res) {
	wl_resource_destroy(res);
}

static const struct wl_subcompositor_interface subcompositor_impl;

static struct wlr_subcompositor *subcompositor_from_resource(struct wl_resource *res) {
	assert(wl_resource_instance_of(res, &wl_subcompositor_interface,
		&subcompositor_impl));
	return wl_resource_get_user_data(res);
}

static void wlr_subsurface_destroy(struct wlr_subsurface *ss)
{
	wlr_signal_emit_safe(&ss->events.destroy, ss);

	ss->surface->role_data = NULL;
	wl_list_remove(&ss->surface_destroy.link);
	if (ss->parent) {
		wl_list_remove(&ss->parent_destroy.link);
	}
	free(ss);
}

static void subsurface_resource_destroy(struct wl_resource *res) {
	struct wlr_subsurface *ss = subsurface_from_resource(res);

	if (ss) {
		wlr_subsurface_destroy(ss);
	}
}

static void subsurface_surface_destroy(struct wl_listener *listener, void *data) {
	struct wlr_subsurface *ss = wl_container_of(listener, ss, surface_destroy);

	/* Make resource inert */
	wl_resource_set_user_data(ss->resource, NULL);

	wlr_subsurface_destroy(ss);
}

static void subsurface_parent_destroy(struct wl_listener *listener, void *data) {
	struct wlr_subsurface *ss = wl_container_of(listener, ss, parent_destroy);

	wl_list_remove(&ss->parent_destroy.link);
	ss->parent = NULL;
}

static void state_subsurface_destroy(struct wl_listener *listener, void *data) {
	struct subsurface_state *st = wl_container_of(listener, st, subsurface_destroy);

	wl_list_remove(&st->subsurface_destroy.link);
	st->subsurface = NULL;

	/* We don't have a surface or saved commit, so this is now useless */
	if (!st->commit) {
		wl_list_remove(&st->link);
		free(st);
	}
}

static void subcompositor_get_subsurface(struct wl_client *client,
		struct wl_resource *sc_res, uint32_t id,
		struct wl_resource *surface_res, struct wl_resource *parent_res) {
	struct wlr_subcompositor *sc = subcompositor_from_resource(sc_res);
	struct wlr_surface_2 *surface = wlr_surface_from_resource_2(surface_res);
	struct wlr_surface_2 *parent = wlr_surface_from_resource_2(parent_res);

	/* Check for protocol violations */

	if (surface == parent) {
		wl_resource_post_error(sc_res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"surface cannot be its own parent");
		return;
	}

	if (!wlr_surface_set_role_2(surface, "wl_subsurface")) {
		wl_resource_post_error(sc_res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"surface already has role");
		return;
	}

	if (surface->role_data != NULL) {
		wl_resource_post_error(sc_res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"surface is already a sub-surface");
		return;
	}

	if (wlr_surface_get_root_surface(parent) == surface) {
		wl_resource_post_error(sc_res, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
			"surface is an ancestor of parent");
		return;
	}

	/* Add role state for surface */

	struct wlr_subsurface *ss = calloc(1, sizeof(*ss));
	if (!ss) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error_post;
	}

	ss->resource = wl_resource_create(client, &wl_subsurface_interface,
		wl_resource_get_version(sc_res), id);
	if (!ss->resource) {
		wlr_log_errno(WLR_ERROR, "Failed to create subsurface resource");
		goto error_ss;
	}

	ss->surface = surface;
	ss->parent = parent;
	ss->synchronized = true;
	wl_signal_init(&ss->events.destroy);
	ss->surface_destroy.notify = subsurface_surface_destroy;
	wl_signal_add(&surface->events.destroy, &ss->surface_destroy);
	ss->parent_destroy.notify = subsurface_parent_destroy;
	wl_signal_add(&parent->events.destroy, &ss->parent_destroy);

	surface->role_data = ss;

	/* Insert into parent's sub-surface tree */

	struct wlr_commit *parent_commit = wlr_surface_get_pending(parent);
	struct subsurface_root *root = wlr_commit_get(parent_commit, sc->root_id);
	assert(root);

	struct subsurface_state *st = calloc(1, sizeof(*st));
	if (!st) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error_res;
	}

	st->subsurface = ss;
	st->subsurface_destroy.notify = state_subsurface_destroy;
	wl_signal_add(&ss->events.destroy, &st->subsurface_destroy);
	wl_list_insert(root->subsurfaces.prev, &st->link);

	wl_resource_set_implementation(ss->resource, &subsurface_impl,
		ss, subsurface_resource_destroy);
	return;

error_res:
	wl_resource_destroy(ss->resource);
	wl_list_remove(&ss->surface_destroy.link);
	wl_list_remove(&ss->parent_destroy.link);
error_ss:
	free(ss);
error_post:
	wl_resource_post_no_memory(sc_res);
}

static const struct wl_subcompositor_interface subcompositor_impl = {
	.destroy = subcompositor_destroy,
	.get_subsurface = subcompositor_get_subsurface,
};

static void subcompositor_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_subcompositor *sc = data;

	struct wl_resource *res =
		wl_resource_create(client, &wl_subcompositor_interface, version, id);
	if (!res) {
		wlr_log_errno(WLR_ERROR, "Failed to create subcompositor resource");
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(res, &subcompositor_impl, sc, NULL);
}

static void subsurface_complete(struct wl_listener *listener, void *data) {
	struct subsurface_state *st = wl_container_of(listener, st, commit_complete);

	st->inhibiting_parent = false;
	wl_list_remove(&st->commit_complete.link);

	wlr_commit_uninhibit(st->parent);
	st->parent = NULL;
}

static void commit_committed(struct wl_listener *listener, void *data) {
	struct subsurface_root *root = wl_container_of(listener, root, commit_committed);
	struct wlr_commit *parent = data;

	struct subsurface_state *iter;
	wl_list_for_each(iter, &root->subsurfaces, link) {
		if (iter == &root->root) {
			continue;
		}

		struct wlr_subsurface *ss = iter->subsurface;

		iter->synchronized = ss->synchronized;
		if (!iter->synchronized) {
			continue;
		}

		iter->commit = wlr_surface_get_latest(ss->surface);
		if (iter->commit && !wlr_commit_is_complete(iter->commit)) {
			iter->parent = parent;
			iter->inhibiting_parent = true;
			wlr_commit_inhibit(parent);

			iter->commit_complete.notify = subsurface_complete;
			wl_signal_add(&iter->commit->events.complete, &iter->commit_complete);
		}
	}
}

static void commit_destroy(struct wl_listener *listener, void *data) {
	struct subsurface_root *root = wl_container_of(listener, root, commit_destroy);

	struct subsurface_state *iter, *tmp;
	wl_list_for_each_safe(iter, tmp, &root->subsurfaces, link) {
		if (iter == &root->root) {
			continue;
		}

		if (iter->subsurface) {
			wl_list_remove(&iter->subsurface_destroy.link);
		}
		if (iter->inhibiting_parent) {
			wl_list_remove(&iter->commit_complete.link);
		}
		if (iter->commit) {
			wlr_commit_unref(iter->commit);
		}
		free(iter);
	}

	wl_list_remove(&root->commit_destroy.link);
	free(root);
}

static void subcompositor_new_state(struct wl_listener *listener, void *data) {
	struct wlr_subcompositor *sc = wl_container_of(listener, sc, new_state);
	struct wlr_compositor_new_state_args *args = data;
	struct wlr_commit *new = args->new;

	struct subsurface_root *root = calloc(1, sizeof(*root));
	wl_list_init(&root->subsurfaces);
	root->commit_committed.notify = commit_committed;
	wl_signal_add(&new->events.commit, &root->commit_committed);
	root->commit_destroy.notify = commit_destroy;
	wl_signal_add(&new->events.destroy, &root->commit_destroy);

	if (args->old) {
		struct subsurface_root *old_root = wlr_commit_get(args->old, sc->root_id);

		struct subsurface_state *iter;
		wl_list_for_each(iter, &old_root->subsurfaces, link) {
			struct subsurface_state *new;
			if (iter == &old_root->root) {
				new = &root->root;
				new->commit = NULL;
			} else {
				new = calloc(1, sizeof(*new));
				if (new->commit) {
					new->commit = wlr_commit_ref(iter->commit);
				}
			}

			new->subsurface = iter->subsurface;
			new->x = iter->x;
			new->y = iter->y;

			wl_list_insert(root->subsurfaces.prev, &new->link);
		}
	} else {
		wl_list_insert(&root->subsurfaces, &root->root.link);
		root->root.subsurface = NULL;
		root->root.commit = NULL;
		root->root.x = 0;
		root->root.y = 0;
	}

	wlr_commit_set(new, sc->root_id, root);
}

static void subcompositor_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_subcompositor *sc =
		wl_container_of(listener, sc, display_destroy);

	wl_global_destroy(sc->global);
	free(sc);
}

struct wlr_subcompositor *wlr_subcompositor_create(struct wlr_compositor *compositor) {
	struct wlr_subcompositor *sc = calloc(1, sizeof(*sc));
	if (!sc) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	sc->global = wl_global_create(compositor->display, &wl_subcompositor_interface,
		SUBCOMPOSITOR_VERSION, sc, subcompositor_bind);
	if (!sc->global) {
		wlr_log_errno(WLR_ERROR, "Failed to create wayland global");
		free(sc);
		return NULL;
	}

	sc->compositor = compositor;
	sc->root_id = wlr_compositor_register(compositor);

	wl_signal_init(&sc->events.new_subsurface);

	sc->new_state.notify = subcompositor_new_state;
	wl_signal_add(&compositor->events.new_state, &sc->new_state);

	sc->display_destroy.notify = subcompositor_display_destroy;
	wl_display_add_destroy_listener(compositor->display, &sc->display_destroy);

	return sc;
}

struct wlr_subsurface *wlr_surface_to_subsurface(struct wlr_surface_2 *surf) {
	if (surf->role_name && strcmp(surf->role_name, "wl_subsurface") == 0) {
		return surf->role_data;
	}

	return NULL;
}

struct wlr_surface_2 *wlr_surface_get_root_surface(struct wlr_surface_2 *surf) {
	struct wlr_subsurface *ss;

	while (surf && (ss = wlr_surface_to_subsurface(surf))) {
		surf = ss->parent;
	}

	return surf;
}

static void commit_for_each(struct wlr_subcompositor *sc, struct wlr_commit *commit,
		wlr_subsurface_iter_t func, void *userdata, int32_t x, int32_t y) {
	assert(wlr_commit_is_complete(commit));

	struct subsurface_root *root = wlr_commit_get(commit, sc->root_id);
	struct subsurface_state *iter;
	wl_list_for_each(iter, &root->subsurfaces, link) {
		int32_t new_x = iter->x + x;
		int32_t new_y = iter->y + y;

		if (iter == &root->root) {
			func(userdata, commit, new_x, new_y);
		} else if (iter->synchronized && iter->commit) {
			commit_for_each(sc, iter->commit, func, userdata, new_x, new_y);
		} else if (!iter->synchronized) {
			struct wlr_surface_2 *s = iter->subsurface->surface;
			struct wlr_commit *c = wlr_surface_get_commit(s);
			if (c) {
				commit_for_each(sc, c, func, userdata, new_x, new_y);
				wlr_commit_unref(c);
			}
		}
	}
}

void wlr_commit_for_each_subsurface(struct wlr_subcompositor *sc,
		struct wlr_commit *commit, wlr_subsurface_iter_t func, void *userdata) {
	commit_for_each(sc, commit, func, userdata, 0, 0);
}
