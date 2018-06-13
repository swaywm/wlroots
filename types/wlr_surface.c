#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_region.h>
#include <wlr/types/wlr_surface.h>
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
	if (state->buffer) {
		wl_list_remove(&state->buffer_destroy_listener.link);
		state->buffer = NULL;
	}
}

static void surface_handle_buffer_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_surface_state *state =
		wl_container_of(listener, state, buffer_destroy_listener);
	surface_state_reset_buffer(state);
}

static void surface_state_set_buffer(struct wlr_surface_state *state,
		struct wl_resource *buffer) {
	surface_state_reset_buffer(state);

	state->buffer = buffer;
	if (buffer) {
		wl_resource_add_destroy_listener(buffer,
			&state->buffer_destroy_listener);
		state->buffer_destroy_listener.notify = surface_handle_buffer_destroy;
	}
}

static void surface_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *buffer, int32_t sx, int32_t sy) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	surface->pending->invalid |= WLR_SURFACE_INVALID_BUFFER;
	surface->pending->sx = sx;
	surface->pending->sy = sy;
	surface_state_set_buffer(surface->pending, buffer);
}

static void surface_damage(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending->invalid |= WLR_SURFACE_INVALID_SURFACE_DAMAGE;
	pixman_region32_union_rect(&surface->pending->surface_damage,
		&surface->pending->surface_damage,
		x, y, width, height);
}

static struct wlr_frame_callback *frame_callback_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_callback_interface, NULL));
	return wl_resource_get_user_data(resource);
}

static void callback_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_frame_callback *cb = frame_callback_from_resource(resource);
	wl_list_remove(&cb->link);
	free(cb);
}

static void surface_frame(struct wl_client *client,
		struct wl_resource *resource, uint32_t callback) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	struct wlr_frame_callback *cb =
		calloc(1, sizeof(struct wlr_frame_callback));
	if (cb == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	cb->resource = wl_resource_create(client, &wl_callback_interface,
		CALLBACK_VERSION, callback);
	if (cb->resource == NULL) {
		free(cb);
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(cb->resource, NULL, cb,
		callback_handle_resource_destroy);

	wl_list_insert(surface->pending->frame_callback_list.prev, &cb->link);

	surface->pending->invalid |= WLR_SURFACE_INVALID_FRAME_CALLBACK_LIST;
}

static void surface_set_opaque_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	if ((surface->pending->invalid & WLR_SURFACE_INVALID_OPAQUE_REGION)) {
		pixman_region32_clear(&surface->pending->opaque);
	}
	surface->pending->invalid |= WLR_SURFACE_INVALID_OPAQUE_REGION;
	if (region_resource) {
		pixman_region32_t *region = wlr_region_from_resource(region_resource);
		pixman_region32_copy(&surface->pending->opaque, region);
	} else {
		pixman_region32_clear(&surface->pending->opaque);
	}
}

static void surface_set_input_region(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *region_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending->invalid |= WLR_SURFACE_INVALID_INPUT_REGION;
	if (region_resource) {
		pixman_region32_t *region = wlr_region_from_resource(region_resource);
		pixman_region32_copy(&surface->pending->input, region);
	} else {
		pixman_region32_fini(&surface->pending->input);
		pixman_region32_init_rect(&surface->pending->input,
			INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
	}
}

static bool surface_update_size(struct wlr_surface *surface,
		struct wlr_surface_state *state) {
	if (!state->buffer) {
		pixman_region32_union_rect(&state->surface_damage,
			&state->surface_damage, 0, 0, state->width, state->height);
		state->height = 0;
		state->width = 0;
		return true;
	}

	int scale = state->scale;
	enum wl_output_transform transform = state->transform;

	wlr_buffer_get_resource_size(state->buffer, surface->renderer,
		&state->buffer_width, &state->buffer_height);

	int width = state->buffer_width / scale;
	int height = state->buffer_height / scale;

	if (transform == WL_OUTPUT_TRANSFORM_90 ||
		transform == WL_OUTPUT_TRANSFORM_270 ||
		transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
		transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
		int tmp = width;
		width = height;
		height = tmp;
	}

	bool update_damage = false;
	if (width != state->width || height != state->height) {
		// Damage the whole surface on resize
		// This isn't in the spec, but Weston does it and QT expects it
		pixman_region32_union_rect(&state->surface_damage,
			&state->surface_damage, 0, 0, state->width, state->height);
		pixman_region32_union_rect(&state->surface_damage,
			&state->surface_damage, 0, 0, width, height);
		update_damage = true;
	}

	state->width = width;
	state->height = height;

	return update_damage;
}

/**
 * Append pending state to current state and clear pending state.
 */
static void surface_move_state(struct wlr_surface *surface,
		struct wlr_surface_state *next, struct wlr_surface_state *state) {
	bool update_damage = false;
	bool update_size = false;

	int oldw = state->width;
	int oldh = state->height;

	if ((next->invalid & WLR_SURFACE_INVALID_SCALE)) {
		state->scale = next->scale;
		update_size = true;
	}
	if ((next->invalid & WLR_SURFACE_INVALID_TRANSFORM)) {
		state->transform = next->transform;
		update_size = true;
	}
	if ((next->invalid & WLR_SURFACE_INVALID_BUFFER)) {
		surface_state_set_buffer(state, next->buffer);
		surface_state_reset_buffer(next);
		state->sx = next->sx;
		state->sy = next->sy;
		update_size = true;
	}
	if (update_size) {
		update_damage = surface_update_size(surface, state);
	}
	if ((next->invalid & WLR_SURFACE_INVALID_SURFACE_DAMAGE)) {
		pixman_region32_intersect_rect(&next->surface_damage,
			&next->surface_damage, 0, 0, state->width, state->height);
		pixman_region32_union(&state->surface_damage, &state->surface_damage,
			&next->surface_damage);
		pixman_region32_clear(&next->surface_damage);
		update_damage = true;
	}
	if ((next->invalid & WLR_SURFACE_INVALID_BUFFER_DAMAGE)) {
		pixman_region32_intersect_rect(&next->buffer_damage,
			&next->buffer_damage, 0, 0, state->buffer_width,
			state->buffer_height);
		pixman_region32_union(&state->buffer_damage, &state->buffer_damage,
			&next->buffer_damage);
		pixman_region32_clear(&next->buffer_damage);
		update_damage = true;
	}
	if (update_damage) {
		pixman_region32_t buffer_damage, surface_damage;
		pixman_region32_init(&buffer_damage);
		pixman_region32_init(&surface_damage);

		// Surface to buffer damage
		pixman_region32_copy(&buffer_damage, &state->surface_damage);
		wlr_region_transform(&buffer_damage, &buffer_damage,
			wlr_output_transform_invert(state->transform),
			state->width, state->height);
		wlr_region_scale(&buffer_damage, &buffer_damage, state->scale);

		// Buffer to surface damage
		pixman_region32_copy(&surface_damage, &state->buffer_damage);
		wlr_region_transform(&surface_damage, &surface_damage, state->transform,
			state->buffer_width, state->buffer_height);
		wlr_region_scale(&surface_damage, &surface_damage, 1.0f/state->scale);

		pixman_region32_union(&state->buffer_damage, &state->buffer_damage,
			&buffer_damage);
		pixman_region32_union(&state->surface_damage, &state->surface_damage,
			&surface_damage);

		pixman_region32_fini(&buffer_damage);
		pixman_region32_fini(&surface_damage);
	}
	if ((next->invalid & WLR_SURFACE_INVALID_OPAQUE_REGION)) {
		// TODO: process buffer
		pixman_region32_clear(&next->opaque);
	}
	if ((next->invalid & WLR_SURFACE_INVALID_INPUT_REGION)) {
		// TODO: process buffer
		pixman_region32_copy(&state->input, &next->input);
	}
	if ((next->invalid & WLR_SURFACE_INVALID_SUBSURFACE_POSITION)) {
		// Subsurface has moved
		int dx = state->subsurface_position.x - next->subsurface_position.x;
		int dy = state->subsurface_position.y - next->subsurface_position.y;

		state->subsurface_position.x = next->subsurface_position.x;
		state->subsurface_position.y = next->subsurface_position.y;
		next->subsurface_position.x = 0;
		next->subsurface_position.y = 0;

		if (dx != 0 || dy != 0) {
			pixman_region32_union_rect(&state->surface_damage,
				&state->surface_damage, dx, dy, oldw, oldh);
			pixman_region32_union_rect(&state->surface_damage,
				&state->surface_damage, 0, 0, state->width, state->height);
		}
	}
	if ((next->invalid & WLR_SURFACE_INVALID_FRAME_CALLBACK_LIST)) {
		wl_list_insert_list(&state->frame_callback_list,
			&next->frame_callback_list);
		wl_list_init(&next->frame_callback_list);
	}

	state->invalid |= next->invalid;
	next->invalid = 0;
}

static void surface_damage_subsurfaces(struct wlr_subsurface *subsurface) {
	// XXX: This is probably the wrong way to do it, because this damage should
	// come from the client, but weston doesn't do it correctly either and it
	// seems to work ok. See the comment on weston_surface_damage for more info
	// about a better approach.
	struct wlr_surface *surface = subsurface->surface;
	pixman_region32_union_rect(&surface->current->surface_damage,
		&surface->current->surface_damage,
		0, 0, surface->current->width,
		surface->current->height);

	subsurface->reordered = false;

	struct wlr_subsurface *child;
	wl_list_for_each(child, &subsurface->surface->subsurfaces, parent_link) {
		surface_damage_subsurfaces(child);
	}
}

static void surface_apply_damage(struct wlr_surface *surface,
		bool invalid_buffer) {
	struct wl_resource *resource = surface->current->buffer;
	if (resource == NULL) {
		// NULL commit
		wlr_buffer_unref(surface->buffer);
		surface->buffer = NULL;
		surface->texture = NULL;
		return;
	}

	if (surface->buffer != NULL && !surface->buffer->released &&
			!invalid_buffer) {
		// The buffer is still the same, no need to re-upload
		return;
	}

	if (surface->buffer != NULL && surface->buffer->released) {
		pixman_region32_t damage;
		pixman_region32_init(&damage);
		pixman_region32_copy(&damage, &surface->current->buffer_damage);
		pixman_region32_intersect_rect(&damage, &damage, 0, 0,
			surface->current->buffer_width, surface->current->buffer_height);

		struct wlr_buffer *updated_buffer =
			wlr_buffer_apply_damage(surface->buffer, resource, &damage);

		pixman_region32_fini(&damage);

		if (updated_buffer != NULL) {
			surface->buffer = updated_buffer;
			return;
		}
	}

	wlr_buffer_unref(surface->buffer);
	surface->buffer = NULL;
	surface->texture = NULL;

	struct wlr_buffer *buffer = wlr_buffer_create(surface->renderer, resource);
	if (buffer == NULL) {
		wlr_log(L_ERROR, "Failed to upload buffer");
		return;
	}

	surface->buffer = buffer;
	surface->texture = buffer->texture;
}

static void surface_commit_pending(struct wlr_surface *surface) {
	bool invalid_buffer = surface->pending->invalid & WLR_SURFACE_INVALID_BUFFER;

	surface_move_state(surface, surface->pending, surface->current);

	surface_apply_damage(surface, invalid_buffer);

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

	if (surface->role_committed) {
		surface->role_committed(surface, surface->role_data);
	}

	// TODO: add the invalid bitfield to this callback
	wlr_signal_emit_safe(&surface->events.commit, surface);

	pixman_region32_clear(&surface->current->surface_damage);
	pixman_region32_clear(&surface->current->buffer_damage);
}

static bool subsurface_is_synchronized(struct wlr_subsurface *subsurface) {
	while (1) {
		if (subsurface->synchronized) {
			return true;
		}

		if (!subsurface->parent) {
			return false;
		}

		if (!wlr_surface_is_subsurface(subsurface->parent)) {
			break;
		}
		subsurface = wlr_subsurface_from_surface(subsurface->parent);
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
			surface_move_state(surface, subsurface->cached, surface->pending);
			surface_commit_pending(surface);
			subsurface->has_cache = false;
			subsurface->cached->invalid = 0;
		}

		struct wlr_subsurface *tmp;
		wl_list_for_each(tmp, &surface->subsurfaces, parent_link) {
			subsurface_parent_commit(tmp, true);
		}
	}
}

static void subsurface_commit(struct wlr_subsurface *subsurface) {
	struct wlr_surface *surface = subsurface->surface;

	if (subsurface_is_synchronized(subsurface)) {
		surface_move_state(surface, surface->pending, subsurface->cached);
		subsurface->has_cache = true;
	} else {
		if (subsurface->has_cache) {
			surface_move_state(surface, subsurface->cached, surface->pending);
			surface_commit_pending(surface);
			subsurface->has_cache = false;

		} else {
			surface_commit_pending(surface);
		}

		struct wlr_subsurface *tmp;
		wl_list_for_each(tmp, &surface->subsurfaces, parent_link) {
			subsurface_parent_commit(tmp, false);
		}
	}
}

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);

	if (wlr_surface_is_subsurface(surface)) {
		struct wlr_subsurface *subsurface =
			wlr_subsurface_from_surface(surface);
		subsurface_commit(subsurface);
		return;
	}

	surface_commit_pending(surface);

	struct wlr_subsurface *tmp;
	wl_list_for_each(tmp, &surface->subsurfaces, parent_link) {
		subsurface_parent_commit(tmp, false);
	}
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending->invalid |= WLR_SURFACE_INVALID_TRANSFORM;
	surface->pending->transform = transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	surface->pending->invalid |= WLR_SURFACE_INVALID_SCALE;
	surface->pending->scale = scale;
}

static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_surface *surface = wlr_surface_from_resource(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending->invalid |= WLR_SURFACE_INVALID_BUFFER_DAMAGE;
	pixman_region32_union_rect(&surface->pending->buffer_damage,
		&surface->pending->buffer_damage,
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

static struct wlr_surface_state *surface_state_create(void) {
	struct wlr_surface_state *state =
		calloc(1, sizeof(struct wlr_surface_state));
	if (state == NULL) {
		return NULL;
	}
	state->scale = 1;
	state->transform = WL_OUTPUT_TRANSFORM_NORMAL;

	wl_list_init(&state->frame_callback_list);

	pixman_region32_init(&state->surface_damage);
	pixman_region32_init(&state->buffer_damage);
	pixman_region32_init(&state->opaque);
	pixman_region32_init_rect(&state->input,
		INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);

	return state;
}

static void surface_state_destroy(struct wlr_surface_state *state) {
	surface_state_reset_buffer(state);
	struct wlr_frame_callback *cb, *tmp;
	wl_list_for_each_safe(cb, tmp, &state->frame_callback_list, link) {
		wl_resource_destroy(cb->resource);
	}

	pixman_region32_fini(&state->surface_damage);
	pixman_region32_fini(&state->buffer_damage);
	pixman_region32_fini(&state->opaque);
	pixman_region32_fini(&state->input);

	free(state);
}

static void subsurface_destroy(struct wlr_subsurface *subsurface) {
	if (subsurface == NULL) {
		return;
	}

	wlr_signal_emit_safe(&subsurface->events.destroy, subsurface);

	wl_list_remove(&subsurface->surface_destroy.link);
	surface_state_destroy(subsurface->cached);

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
	surface_state_destroy(surface->pending);
	surface_state_destroy(surface->current);
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

	wlr_log(L_DEBUG, "New wlr_surface %p (res %p)", surface, surface->resource);

	surface->renderer = renderer;

	surface->current = surface_state_create();
	surface->pending = surface_state_create();

	wl_signal_init(&surface->events.commit);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.new_subsurface);
	wl_list_init(&surface->subsurfaces);
	wl_list_init(&surface->subsurface_pending_list);

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

bool wlr_surface_has_buffer(struct wlr_surface *surface) {
	return surface->texture != NULL;
}

int wlr_surface_set_role(struct wlr_surface *surface, const char *role,
		struct wl_resource *error_resource, uint32_t error_code) {
	assert(role);

	if (surface->role == NULL ||
			surface->role == role ||
			strcmp(surface->role, role) == 0) {
		surface->role = role;

		return 0;
	}

	wl_resource_post_error(error_resource, error_code,
		"Cannot assign role %s to wl_surface@%d, already has role %s\n",
		role,
		wl_resource_get_id(surface->resource),
		surface->role);

	return -1;
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

	struct wlr_surface *surface = subsurface->surface;
	surface->pending->invalid |= WLR_SURFACE_INVALID_SUBSURFACE_POSITION;
	surface->pending->subsurface_position.x = x;
	surface->pending->subsurface_position.y = y;
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
	subsurface->cached = surface_state_create();
	if (subsurface->cached == NULL) {
		free(subsurface);
		wl_client_post_no_memory(client);
		return NULL;
	}
	subsurface->synchronized = true;
	subsurface->surface = surface;
	subsurface->resource =
		wl_resource_create(client, &wl_subsurface_interface, version, id);
	if (subsurface->resource == NULL) {
		surface_state_destroy(subsurface->cached);
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
			wlr_subsurface_from_surface(surface);
		surface = subsurface->parent;
	}
	return surface;
}

bool wlr_surface_point_accepts_input(struct wlr_surface *surface,
		double sx, double sy) {
	return sx >= 0 && sx <= surface->current->width &&
		sy >= 0 && sy <= surface->current->height &&
		pixman_region32_contains_point(&surface->current->input, sx, sy, NULL);
}

struct wlr_surface *wlr_surface_surface_at(struct wlr_surface *surface,
		double sx, double sy, double *sub_x, double *sub_y) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each_reverse(subsurface, &surface->subsurfaces, parent_link) {
		double _sub_x = subsurface->surface->current->subsurface_position.x;
		double _sub_y = subsurface->surface->current->subsurface_position.y;
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
	wl_resource_for_each(resource, &output->wl_resources) {
		if (client == wl_resource_get_client(resource)) {
			wl_surface_send_enter(surface->resource, resource);
		}
	}
}

void wlr_surface_send_leave(struct wlr_surface *surface,
		struct wlr_output *output) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->wl_resources) {
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
	struct wlr_frame_callback *cb, *cnext;
	wl_list_for_each_safe(cb, cnext, &surface->current->frame_callback_list,
			link) {
		wl_callback_send_done(cb->resource, timespec_to_msec(when));
		wl_resource_destroy(cb->resource);
	}
}

void wlr_surface_set_role_committed(struct wlr_surface *surface,
		void (*role_committed)(struct wlr_surface *surface, void *role_data),
		void *role_data) {
	surface->role_committed = role_committed;
	surface->role_data = role_data;
}

static void surface_for_each_surface(struct wlr_surface *surface, int x, int y,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	iterator(surface, x, y, user_data);

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurfaces, parent_link) {
		struct wlr_surface_state *state = subsurface->surface->current;
		int sx = state->subsurface_position.x;
		int sy = state->subsurface_position.y;

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

	acc->max_x = max(x + surface->current->width, acc->max_x);
	acc->max_y = max(y + surface->current->height, acc->max_y);
}

void wlr_surface_get_extends(struct wlr_surface *surface, struct wlr_box *box) {
	struct bound_acc acc = {
		.min_x = 0,
		.min_y = 0,
		.max_x = surface->current->width,
		.max_y = surface->current->height,
	};

	wlr_surface_for_each_surface(surface, handle_bounding_box_surface, &acc);

	box->x = acc.min_x;
	box->y = acc.min_y;
	box->width = acc.max_x - acc.min_x;
	box->height = acc.max_y - acc.min_y;
}
