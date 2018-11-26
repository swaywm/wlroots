#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_region.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "util/signal.h"

#define CALLBACK_VERSION 1
#define SURFACE_VERSION 4
#define SUBSURFACE_VERSION 1

static int min(int fst, int snd) {
	if (fst < snd) {
		return fst;
	} else {
		return snd;
	}
}

static int max(int fst, int snd) {
	if (fst > snd) {
		return fst;
	} else {
		return snd;
	}
}

static void surface_state_reset_buffer(struct wlr_surface_state *state) {
	if (state->buffer_resource) {
		wl_list_remove(&state->buffer_destroy.link);
		state->buffer_resource = NULL;
	}
}

static void surface_handle_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_surface_state *state =
		wl_container_of(listener, state, buffer_destroy);
	surface_state_reset_buffer(state);
}

static void surface_state_set_buffer(struct wlr_surface_state *state,
		struct wl_resource *buffer_resource) {
	surface_state_reset_buffer(state);

	state->buffer_resource = buffer_resource;
	if (buffer_resource != NULL) {
		wl_resource_add_destroy_listener(buffer_resource,
			&state->buffer_destroy);
		state->buffer_destroy.notify = surface_handle_buffer_destroy;
	}
}

static void surface_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *buffer, int32_t dx, int32_t dy) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	surface->pending.committed |= WLR_SURFACE_STATE_BUFFER;
	surface->pending.dx = dx;
	surface->pending.dy = dy;
	surface_state_set_buffer(&surface->pending, buffer);
}

static void surface_damage(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending.committed |= WLR_SURFACE_STATE_SURFACE_DAMAGE;
	pixman_region32_union_rect(&surface->pending.surface_damage,
		&surface->pending.surface_damage,
		x, y, width, height);
}

static void callback_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void surface_frame(struct wl_client *client,
		struct wl_resource *resource, uint32_t callback) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	struct wl_resource *callback_resource = wl_resource_create(client,
		&wl_callback_interface, CALLBACK_VERSION, callback);
	if (callback_resource == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(callback_resource, NULL, NULL,
		callback_handle_resource_destroy);

	wl_list_insert(surface->pending.frame_callback_list.prev,
		wl_resource_get_link(callback_resource));

	surface->pending.committed |= WLR_SURFACE_STATE_FRAME_CALLBACK_LIST;
}

static void surface_set_opaque_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending.committed |= WLR_SURFACE_STATE_OPAQUE_REGION;
	if (region_resource) {
		pixman_region32_t *region = wlr_region_from_resource(region_resource);
		pixman_region32_copy(&surface->pending.opaque, region);
	} else {
		pixman_region32_clear(&surface->pending.opaque);
	}
}

static void surface_set_input_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending.committed |= WLR_SURFACE_STATE_INPUT_REGION;
	if (region_resource) {
		pixman_region32_t *region = wlr_region_from_resource(region_resource);
		pixman_region32_copy(&surface->pending.input, region);
	} else {
		pixman_region32_fini(&surface->pending.input);
		pixman_region32_init_rect(&surface->pending.input,
			INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
	}
}

static void surface_state_finalize(struct wlr_surface *surface,
		struct wlr_surface_state *state) {
	if ((state->committed & WLR_SURFACE_STATE_BUFFER)) {
		if (state->buffer_resource != NULL) {
			wlr_buffer_get_resource_size(state->buffer_resource,
				surface->renderer, &state->buffer_width, &state->buffer_height);
		} else {
			state->buffer_width = state->buffer_height = 0;
		}
	}

	int width = state->buffer_width / state->scale;
	int height = state->buffer_height / state->scale;
	if ((state->transform & WL_OUTPUT_TRANSFORM_90) != 0) {
		int tmp = width;
		width = height;
		height = tmp;
	}
	state->width = width;
	state->height = height;

	pixman_region32_intersect_rect(&state->surface_damage,
		&state->surface_damage, 0, 0, state->width, state->height);

	pixman_region32_intersect_rect(&state->buffer_damage,
		&state->buffer_damage, 0, 0, state->buffer_width,
		state->buffer_height);
}

static void surface_update_damage(pixman_region32_t *buffer_damage,
		struct wlr_surface_state *current, struct wlr_surface_state *pending) {
	pixman_region32_clear(buffer_damage);

	if (pending->width != current->width ||
			pending->height != current->height) {
		// Damage the whole buffer on resize
		pixman_region32_union_rect(buffer_damage, buffer_damage, 0, 0,
			pending->buffer_width, pending->buffer_height);
	} else {
		// Copy over surface damage + buffer damage
		pixman_region32_t surface_damage;
		pixman_region32_init(&surface_damage);

		pixman_region32_copy(&surface_damage, &pending->surface_damage);
		wlr_region_transform(&surface_damage, &surface_damage,
			wlr_output_transform_invert(pending->transform),
			pending->width, pending->height);
		wlr_region_scale(&surface_damage, &surface_damage, pending->scale);

		pixman_region32_union(buffer_damage,
			&pending->buffer_damage, &surface_damage);

		pixman_region32_fini(&surface_damage);
	}
}

static void surface_state_copy(struct wlr_surface_state *state,
		struct wlr_surface_state *next) {
	state->width = next->width;
	state->height = next->height;
	state->buffer_width = next->buffer_width;
	state->buffer_height = next->buffer_height;

	if (next->committed & WLR_SURFACE_STATE_SCALE) {
		state->scale = next->scale;
	}
	if (next->committed & WLR_SURFACE_STATE_TRANSFORM) {
		state->transform = next->transform;
	}
	if (next->committed & WLR_SURFACE_STATE_BUFFER) {
		state->dx = next->dx;
		state->dy = next->dy;
	} else {
		state->dx = state->dy = 0;
	}
	if (next->committed & WLR_SURFACE_STATE_SURFACE_DAMAGE) {
		pixman_region32_copy(&state->surface_damage, &next->surface_damage);
	} else {
		pixman_region32_clear(&state->surface_damage);
	}
	if (next->committed & WLR_SURFACE_STATE_BUFFER_DAMAGE) {
		pixman_region32_copy(&state->buffer_damage, &next->buffer_damage);
	} else {
		pixman_region32_clear(&state->buffer_damage);
	}
	if (next->committed & WLR_SURFACE_STATE_OPAQUE_REGION) {
		pixman_region32_copy(&state->opaque, &next->opaque);
	}
	if (next->committed & WLR_SURFACE_STATE_INPUT_REGION) {
		pixman_region32_copy(&state->input, &next->input);
	}

	state->committed |= next->committed;
}

/**
 * Append pending state to current state and clear pending state.
 */
static void surface_state_move(struct wlr_surface_state *state,
		struct wlr_surface_state *next) {
	surface_state_copy(state, next);

	if (next->committed & WLR_SURFACE_STATE_BUFFER) {
		surface_state_set_buffer(state, next->buffer_resource);
		surface_state_reset_buffer(next);
		next->dx = next->dy = 0;
	}
	if (next->committed & WLR_SURFACE_STATE_SURFACE_DAMAGE) {
		pixman_region32_clear(&next->surface_damage);
	}
	if (next->committed & WLR_SURFACE_STATE_BUFFER_DAMAGE) {
		pixman_region32_clear(&next->buffer_damage);
	}
	if (next->committed & WLR_SURFACE_STATE_FRAME_CALLBACK_LIST) {
		wl_list_insert_list(&state->frame_callback_list,
			&next->frame_callback_list);
		wl_list_init(&next->frame_callback_list);
	}

	next->committed = 0;
}

static void surface_damage_subsurfaces(struct wlr_subsurface *subsurface) {
	// XXX: This is probably the wrong way to do it, because this damage should
	// come from the client, but weston doesn't do it correctly either and it
	// seems to work ok. See the comment on weston_surface_damage for more info
	// about a better approach.
	struct wlr_surface *surface = subsurface->surface;
	pixman_region32_union_rect(&surface->buffer_damage,
		&surface->buffer_damage, 0, 0,
		surface->current.buffer_width, surface->current.buffer_height);

	subsurface->reordered = false;

	struct wlr_subsurface *child;
	wl_list_for_each(child, &subsurface->surface->subsurfaces, parent_link) {
		surface_damage_subsurfaces(child);
	}
}

static void surface_apply_damage(struct wlr_surface *surface) {
	struct wl_resource *resource = surface->current.buffer_resource;
	if (resource == NULL) {
		// NULL commit
		wlr_buffer_unref(surface->buffer);
		surface->buffer = NULL;
		return;
	}

	if (surface->buffer != NULL && surface->buffer->released) {
		struct wlr_buffer *updated_buffer = wlr_buffer_apply_damage(
			surface->buffer, resource, &surface->buffer_damage);
		if (updated_buffer != NULL) {
			surface->buffer = updated_buffer;
			return;
		}
	}

	wlr_buffer_unref(surface->buffer);
	surface->buffer = NULL;

	struct wlr_buffer *buffer = wlr_buffer_create(surface->renderer, resource);
	if (buffer == NULL) {
		wlr_log(WLR_ERROR, "Failed to upload buffer");
		return;
	}

	surface->buffer = buffer;
}

static void surface_update_opaque_region(struct wlr_surface *surface) {
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL) {
		pixman_region32_clear(&surface->opaque_region);
		return;
	}

	if (wlr_texture_is_opaque(texture)) {
		pixman_region32_init_rect(&surface->opaque_region,
			0, 0, surface->current.width, surface->current.height);
		return;
	}

	pixman_region32_intersect_rect(&surface->opaque_region,
		&surface->current.opaque,
		0, 0, surface->current.width, surface->current.height);
}

static void surface_update_input_region(struct wlr_surface *surface) {
	pixman_region32_intersect_rect(&surface->input_region,
		&surface->current.input,
		0, 0, surface->current.width, surface->current.height);
}

static void surface_commit_pending(struct wlr_surface *surface) {
	surface_state_finalize(surface, &surface->pending);

	if (surface->role && surface->role->precommit) {
		surface->role->precommit(surface);
	}

	bool invalid_buffer = surface->pending.committed & WLR_SURFACE_STATE_BUFFER;

	surface->sx += surface->pending.dx;
	surface->sy += surface->pending.dy;
	surface_update_damage(&surface->buffer_damage,
		&surface->current, &surface->pending);

	surface_state_copy(&surface->previous, &surface->current);
	surface_state_move(&surface->current, &surface->pending);

	if (invalid_buffer) {
		surface_apply_damage(surface);
	}
	surface_update_opaque_region(surface);
	surface_update_input_region(surface);

	// commit subsurface order
	struct wlr_subsurface *subsurface;
	wl_list_for_each_reverse(subsurface, &surface->subsurface_pending_list,
			parent_pending_link) {
		wl_list_remove(&subsurface->parent_link);
		wl_list_insert(&surface->subsurfaces, &subsurface->parent_link);

		if (subsurface->reordered) {
			// TODO: damage all the subsurfaces
			surface_damage_subsurfaces(subsurface);
		}
	}

	if (surface->role && surface->role->commit) {
		surface->role->commit(surface);
	}

	wlr_signal_emit_safe(&surface->events.commit, surface);
}

static bool subsurface_is_synchronized(struct wlr_subsurface *subsurface) {
	while (subsurface != NULL) {
		if (subsurface->synchronized) {
			return true;
		}

		if (!subsurface->parent) {
			return false;
		}

		if (!wlr_surface_is_subsurface(subsurface->parent)) {
			break;
		}
		subsurface = wlr_subsurface_from_wlr_surface(subsurface->parent);
	}

	return false;
}

/**
 * Recursive function to commit the effectively synchronized children.
 */
static void subsurface_parent_commit(struct wlr_subsurface *subsurface,
		bool synchronized) {
	struct wlr_surface *surface = subsurface->surface;
	if (synchronized || subsurface->synchronized) {
		if (subsurface->has_cache) {
			surface_state_move(&surface->pending, &subsurface->cached);
			surface_commit_pending(surface);
			subsurface->has_cache = false;
			subsurface->cached.committed = 0;
		}

		struct wlr_subsurface *subsurface;
		wl_list_for_each(subsurface, &surface->subsurfaces, parent_link) {
			subsurface_parent_commit(subsurface, true);
		}
	}
}

static void subsurface_commit(struct wlr_subsurface *subsurface) {
	struct wlr_surface *surface = subsurface->surface;

	if (subsurface_is_synchronized(subsurface)) {
		surface_state_move(&subsurface->cached, &surface->pending);
		subsurface->has_cache = true;
	} else {
		if (subsurface->has_cache) {
			surface_state_move(&surface->pending, &subsurface->cached);
			surface_commit_pending(surface);
			subsurface->has_cache = false;
		} else {
			surface_commit_pending(surface);
		}
	}
}

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	struct wlr_subsurface *subsurface = wlr_surface_is_subsurface(surface) ?
		wlr_subsurface_from_wlr_surface(surface) : NULL;
	if (subsurface != NULL) {
		subsurface_commit(subsurface);
	} else {
		surface_commit_pending(surface);
	}

	wl_list_for_each(subsurface, &surface->subsurfaces, parent_link) {
		subsurface_parent_commit(subsurface, false);
	}
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending.committed |= WLR_SURFACE_STATE_TRANSFORM;
	surface->pending.transform = transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending.committed |= WLR_SURFACE_STATE_SCALE;
	surface->pending.scale = scale;
}

static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending.committed |= WLR_SURFACE_STATE_BUFFER_DAMAGE;
	pixman_region32_union_rect(&surface->pending.buffer_damage,
		&surface->pending.buffer_damage,
		x, y, width, height);
}

static const struct wl_surface_interface surface_interface = {
	.destroy = surface_destroy,
	.attach = surface_attach,
	.damage = surface_damage,
	.frame = surface_frame,
	.set_opaque_region = surface_set_opaque_region,
	.set_input_region = surface_set_input_region,
	.commit = surface_commit,
	.set_buffer_transform = surface_set_buffer_transform,
	.set_buffer_scale = surface_set_buffer_scale,
	.damage_buffer = surface_damage_buffer
};

struct wlr_surface *wlr_surface_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_surface_interface,
		&surface_interface));
	return wl_resource_get_user_data(resource);
}

static void surface_state_init(struct wlr_surface_state *state) {
	state->scale = 1;
	state->transform = WL_OUTPUT_TRANSFORM_NORMAL;

	wl_list_init(&state->frame_callback_list);

	pixman_region32_init(&state->surface_damage);
	pixman_region32_init(&state->buffer_damage);
	pixman_region32_init(&state->opaque);
	pixman_region32_init_rect(&state->input,
		INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
}

static void surface_state_finish(struct wlr_surface_state *state) {
	surface_state_reset_buffer(state);

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &state->frame_callback_list) {
		wl_resource_destroy(resource);
	}

	pixman_region32_fini(&state->surface_damage);
	pixman_region32_fini(&state->buffer_damage);
	pixman_region32_fini(&state->opaque);
	pixman_region32_fini(&state->input);
}

static void subsurface_destroy(struct wlr_subsurface *subsurface) {
	if (subsurface == NULL) {
		return;
	}

	wlr_signal_emit_safe(&subsurface->events.destroy, subsurface);

	wl_list_remove(&subsurface->surface_destroy.link);
	surface_state_finish(&subsurface->cached);

	if (subsurface->parent) {
		wl_list_remove(&subsurface->parent_link);
		wl_list_remove(&subsurface->parent_pending_link);
		wl_list_remove(&subsurface->parent_destroy.link);
	}

	wl_resource_set_user_data(subsurface->resource, NULL);
	if (subsurface->surface) {
		subsurface->surface->role_data = NULL;
	}
	free(subsurface);
}

static void surface_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	wlr_signal_emit_safe(&surface->events.destroy, surface);

	wl_list_remove(wl_resource_get_link(surface->resource));

	wl_list_remove(&surface->renderer_destroy.link);
	surface_state_finish(&surface->pending);
	surface_state_finish(&surface->current);
	surface_state_finish(&surface->previous);
	pixman_region32_fini(&surface->buffer_damage);
	pixman_region32_fini(&surface->opaque_region);
	pixman_region32_fini(&surface->input_region);
	wlr_buffer_unref(surface->buffer);
	free(surface);
}

static void surface_handle_renderer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_surface *surface =
		wl_container_of(listener, surface, renderer_destroy);
	wl_resource_destroy(surface->resource);
}

struct wlr_surface *wlr_surface_create(struct wl_client *client,
		uint32_t version, uint32_t id, struct wlr_renderer *renderer,
		struct wl_list *resource_list) {
	assert(version <= SURFACE_VERSION);

	struct wlr_surface *surface = calloc(1, sizeof(struct wlr_surface));
	if (!surface) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	surface->resource = wl_resource_create(client, &wl_surface_interface,
		version, id);
	if (surface->resource == NULL) {
		free(surface);
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(surface->resource, &surface_interface,
		surface, surface_handle_resource_destroy);

	wlr_log(WLR_DEBUG, "New wlr_surface %p (res %p)", surface, surface->resource);

	surface->renderer = renderer;

	surface_state_init(&surface->current);
	surface_state_init(&surface->pending);
	surface_state_init(&surface->previous);

	wl_signal_init(&surface->events.commit);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.new_subsurface);
	wl_list_init(&surface->subsurfaces);
	wl_list_init(&surface->subsurface_pending_list);
	pixman_region32_init(&surface->buffer_damage);
	pixman_region32_init(&surface->opaque_region);
	pixman_region32_init(&surface->input_region);

	wl_signal_add(&renderer->events.destroy, &surface->renderer_destroy);
	surface->renderer_destroy.notify = surface_handle_renderer_destroy;

	struct wl_list *resource_link = wl_resource_get_link(surface->resource);
	if (resource_list != NULL) {
		wl_list_insert(resource_list, resource_link);
	} else {
		wl_list_init(resource_link);
	}

	return surface;
}

struct wlr_texture *wlr_surface_get_texture(struct wlr_surface *surface) {
	if (surface->buffer == NULL) {
		return NULL;
	}
	return surface->buffer->texture;
}

bool wlr_surface_has_buffer(struct wlr_surface *surface) {
	return wlr_surface_get_texture(surface) != NULL;
}

bool wlr_surface_set_role(struct wlr_surface *surface,
		const struct wlr_surface_role *role, void *role_data,
		struct wl_resource *error_resource, uint32_t error_code) {
	assert(role != NULL);

	if (surface->role != NULL && surface->role != role) {
		if (error_resource != NULL) {
			wl_resource_post_error(error_resource, error_code,
				"Cannot assign role %s to wl_surface@%d, already has role %s\n",
				role->name, wl_resource_get_id(surface->resource),
				surface->role->name);
		}
		return false;
	}

	assert(surface->role_data == NULL);
	surface->role = role;
	surface->role_data = role_data;
	return true;
}

static const struct wl_subsurface_interface subsurface_implementation;

static struct wlr_subsurface *subsurface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_subsurface_interface,
		&subsurface_implementation));
	return wl_resource_get_user_data(resource);
}

static void subsurface_resource_destroy(struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	wl_list_remove(wl_resource_get_link(resource));
	subsurface_destroy(subsurface);
}

static void subsurface_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void subsurface_handle_set_position(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	subsurface->pending.x = x;
	subsurface->pending.y = y;
}

static struct wlr_subsurface *subsurface_find_sibling(
		struct wlr_subsurface *subsurface, struct wlr_surface *surface) {
	struct wlr_surface *parent = subsurface->parent;

	struct wlr_subsurface *sibling;
	wl_list_for_each(sibling, &parent->subsurfaces, parent_link) {
		if (sibling->surface == surface && sibling != subsurface) {
			return sibling;
		}
	}

	return NULL;
}

static void subsurface_handle_place_above(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling_resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	struct wlr_surface *sibling_surface =
		wlr_surface_from_resource(sibling_resource);
	struct wlr_subsurface *sibling =
		subsurface_find_sibling(subsurface, sibling_surface);

	if (!sibling) {
		wl_resource_post_error(subsurface->resource,
			WL_SUBSURFACE_ERROR_BAD_SURFACE,
			"%s: wl_surface@%d is not a parent or sibling",
			"place_above", wl_resource_get_id(sibling_surface->resource));
		return;
	}

	wl_list_remove(&subsurface->parent_pending_link);
	wl_list_insert(&sibling->parent_pending_link,
		&subsurface->parent_pending_link);

	subsurface->reordered = true;
}

static void subsurface_handle_place_below(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling_resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	struct wlr_surface *sibling_surface =
		wlr_surface_from_resource(sibling_resource);
	struct wlr_subsurface *sibling =
		subsurface_find_sibling(subsurface, sibling_surface);

	if (!sibling) {
		wl_resource_post_error(subsurface->resource,
			WL_SUBSURFACE_ERROR_BAD_SURFACE,
			"%s: wl_surface@%d is not a parent or sibling",
			"place_below", wl_resource_get_id(sibling_surface->resource));
		return;
	}

	wl_list_remove(&subsurface->parent_pending_link);
	wl_list_insert(sibling->parent_pending_link.prev,
		&subsurface->parent_pending_link);

	subsurface->reordered = true;
}

static void subsurface_handle_set_sync(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	subsurface->synchronized = true;
}

static void subsurface_handle_set_desync(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = subsurface_from_resource(resource);
	if (subsurface == NULL) {
		return;
	}

	if (subsurface->synchronized) {
		subsurface->synchronized = false;

		if (!subsurface_is_synchronized(subsurface)) {
			// TODO: do a synchronized commit to flush the cache
			subsurface_parent_commit(subsurface, true);
		}
	}
}

static const struct wl_subsurface_interface subsurface_implementation = {
	.destroy = subsurface_handle_destroy,
	.set_position = subsurface_handle_set_position,
	.place_above = subsurface_handle_place_above,
	.place_below = subsurface_handle_place_below,
	.set_sync = subsurface_handle_set_sync,
	.set_desync = subsurface_handle_set_desync,
};

static void subsurface_role_commit(struct wlr_surface *surface) {
	struct wlr_subsurface *subsurface =
		wlr_subsurface_from_wlr_surface(surface);
	if (subsurface == NULL) {
		return;
	}

	if (subsurface->current.x != subsurface->pending.x ||
			subsurface->current.y != subsurface->pending.y) {
		// Subsurface has moved
		int dx = subsurface->current.x - subsurface->pending.x;
		int dy = subsurface->current.y - subsurface->pending.y;

		subsurface->current.x = subsurface->pending.x;
		subsurface->current.y = subsurface->pending.y;

		if ((surface->current.transform & WL_OUTPUT_TRANSFORM_90) != 0) {
			int tmp = dx;
			dx = dy;
			dy = tmp;
		}

		pixman_region32_union_rect(&surface->buffer_damage,
			&surface->buffer_damage,
			dx * surface->previous.scale, dy * surface->previous.scale,
			surface->previous.buffer_width, surface->previous.buffer_height);
		pixman_region32_union_rect(&surface->buffer_damage,
			&surface->buffer_damage, 0, 0,
			surface->current.buffer_width, surface->current.buffer_height);
	}
}

const struct wlr_surface_role subsurface_role = {
	.name = "wl_subsurface",
	.commit = subsurface_role_commit,
};

static void subsurface_handle_parent_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_subsurface *subsurface =
		wl_container_of(listener, subsurface, parent_destroy);
	wl_list_remove(&subsurface->parent_link);
	wl_list_remove(&subsurface->parent_pending_link);
	wl_list_remove(&subsurface->parent_destroy.link);
	subsurface->parent = NULL;
}

static void subsurface_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_subsurface *subsurface =
		wl_container_of(listener, subsurface, surface_destroy);
	subsurface_destroy(subsurface);
}

struct wlr_subsurface *wlr_subsurface_create(struct wlr_surface *surface,
		struct wlr_surface *parent, uint32_t version, uint32_t id,
		struct wl_list *resource_list) {
	assert(version <= SUBSURFACE_VERSION);

	struct wl_client *client = wl_resource_get_client(surface->resource);

	struct wlr_subsurface *subsurface =
		calloc(1, sizeof(struct wlr_subsurface));
	if (!subsurface) {
		wl_client_post_no_memory(client);
		return NULL;
	}
	surface_state_init(&subsurface->cached);
	subsurface->synchronized = true;
	subsurface->surface = surface;
	subsurface->resource =
		wl_resource_create(client, &wl_subsurface_interface, version, id);
	if (subsurface->resource == NULL) {
		surface_state_finish(&subsurface->cached);
		free(subsurface);
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(subsurface->resource,
		&subsurface_implementation, subsurface,
		subsurface_resource_destroy);

	wl_signal_init(&subsurface->events.destroy);

	wl_signal_add(&surface->events.destroy, &subsurface->surface_destroy);
	subsurface->surface_destroy.notify = subsurface_handle_surface_destroy;

	// link parent
	subsurface->parent = parent;
	wl_signal_add(&parent->events.destroy, &subsurface->parent_destroy);
	subsurface->parent_destroy.notify = subsurface_handle_parent_destroy;
	wl_list_insert(parent->subsurfaces.prev, &subsurface->parent_link);
	wl_list_insert(parent->subsurface_pending_list.prev,
		&subsurface->parent_pending_link);

	surface->role_data = subsurface;

	struct wl_list *resource_link = wl_resource_get_link(subsurface->resource);
	if (resource_list != NULL) {
		wl_list_insert(resource_list, resource_link);
	} else {
		wl_list_init(resource_link);
	}

	wlr_signal_emit_safe(&parent->events.new_subsurface, subsurface);

	return subsurface;
}


struct wlr_surface *wlr_surface_get_root_surface(struct wlr_surface *surface) {
	while (wlr_surface_is_subsurface(surface)) {
		struct wlr_subsurface *subsurface =
			wlr_subsurface_from_wlr_surface(surface);
		if (subsurface == NULL) {
			break;
		}
		surface = subsurface->parent;
	}
	return surface;
}

bool wlr_surface_point_accepts_input(struct wlr_surface *surface,
		double sx, double sy) {
	return sx >= 0 && sx < surface->current.width &&
		sy >= 0 && sy < surface->current.height &&
		pixman_region32_contains_point(&surface->current.input, floor(sx), floor(sy), NULL);
}

struct wlr_surface *wlr_surface_surface_at(struct wlr_surface *surface,
		double sx, double sy, double *sub_x, double *sub_y) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each_reverse(subsurface, &surface->subsurfaces, parent_link) {
		double _sub_x = subsurface->current.x;
		double _sub_y = subsurface->current.y;
		struct wlr_surface *sub = wlr_surface_surface_at(subsurface->surface,
			sx - _sub_x, sy - _sub_y, sub_x, sub_y);
		if (sub != NULL) {
			return sub;
		}
	}

	if (wlr_surface_point_accepts_input(surface, sx, sy)) {
		*sub_x = sx;
		*sub_y = sy;
		return surface;
	}

	return NULL;
}

void wlr_surface_send_enter(struct wlr_surface *surface,
		struct wlr_output *output) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		if (client == wl_resource_get_client(resource)) {
			wl_surface_send_enter(surface->resource, resource);
		}
	}
}

void wlr_surface_send_leave(struct wlr_surface *surface,
		struct wlr_output *output) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->resources) {
		if (client == wl_resource_get_client(resource)) {
			wl_surface_send_leave(surface->resource, resource);
		}
	}
}

static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

void wlr_surface_send_frame_done(struct wlr_surface *surface,
		const struct timespec *when) {
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp,
			&surface->current.frame_callback_list) {
		wl_callback_send_done(resource, timespec_to_msec(when));
		wl_resource_destroy(resource);
	}
}

static void surface_for_each_surface(struct wlr_surface *surface, int x, int y,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	iterator(surface, x, y, user_data);

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurfaces, parent_link) {
		struct wlr_subsurface_state *state = &subsurface->current;
		int sx = state->x;
		int sy = state->y;

		surface_for_each_surface(subsurface->surface, x + sx, y + sy,
			iterator, user_data);
	}
}

void wlr_surface_for_each_surface(struct wlr_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	surface_for_each_surface(surface, 0, 0, iterator, user_data);
}

struct bound_acc {
	int32_t min_x, min_y;
	int32_t max_x, max_y;
};

static void handle_bounding_box_surface(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct bound_acc *acc = data;

	acc->min_x = min(x, acc->min_x);
	acc->min_y = min(y, acc->min_y);

	acc->max_x = max(x + surface->current.width, acc->max_x);
	acc->max_y = max(y + surface->current.height, acc->max_y);
}

void wlr_surface_get_extends(struct wlr_surface *surface, struct wlr_box *box) {
	struct bound_acc acc = {
		.min_x = 0,
		.min_y = 0,
		.max_x = surface->current.width,
		.max_y = surface->current.height,
	};

	wlr_surface_for_each_surface(surface, handle_bounding_box_surface, &acc);

	box->x = acc.min_x;
	box->y = acc.min_y;
	box->width = acc.max_x - acc.min_x;
	box->height = acc.max_y - acc.min_y;
}

void wlr_surface_get_effective_damage(struct wlr_surface *surface,
		pixman_region32_t *damage) {
	pixman_region32_clear(damage);

	// Transform and copy the buffer damage in terms of surface coordinates.
	wlr_region_transform(damage, &surface->buffer_damage,
		surface->current.transform, surface->current.buffer_width,
		surface->current.buffer_height);
	wlr_region_scale(damage, damage, 1.0 / (float)surface->current.scale);

	// On resize, damage the previous bounds of the surface. The current bounds
	// have already been damaged in surface_update_damage.
	if (surface->previous.width > surface->current.width ||
			surface->previous.height > surface->current.height) {
		pixman_region32_union_rect(damage, damage, 0, 0,
			surface->previous.width, surface->previous.height);
	}

	// On move, damage where the surface was with its old dimensions.
	if (surface->current.dx != 0 || surface->current.dy != 0) {
		int prev_x = -surface->current.dx;
		int prev_y = -surface->current.dy;
		if ((surface->previous.transform & WL_OUTPUT_TRANSFORM_90) != 0) {
			int temp = prev_x;
			prev_x = prev_y;
			prev_y = temp;
		}
		pixman_region32_union_rect(damage, damage, prev_x, prev_y,
			surface->previous.width, surface->previous.height);
	}
}
