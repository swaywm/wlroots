#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/render/egl.h>
#include <wlr/render/matrix.h>

static void wlr_surface_state_reset_buffer(struct wlr_surface_state *state) {
	if (state->buffer) {
		wl_list_remove(&state->buffer_destroy_listener.link);
		state->buffer = NULL;
	}
}

static void buffer_destroy(struct wl_listener *listener, void *data) {
	struct wlr_surface_state *state =
		wl_container_of(listener, state, buffer_destroy_listener);

	wl_list_remove(&state->buffer_destroy_listener.link);
	state->buffer = NULL;
}

static void wlr_surface_state_release_buffer(struct wlr_surface_state *state) {
	if (state->buffer) {
		wl_resource_post_event(state->buffer, WL_BUFFER_RELEASE);
		wl_list_remove(&state->buffer_destroy_listener.link);
		state->buffer = NULL;
	}
}

static void wlr_surface_state_set_buffer(struct wlr_surface_state *state,
		struct wl_resource *buffer) {
	state->buffer = buffer;
	if (buffer) {
		wl_resource_add_destroy_listener(buffer,
			&state->buffer_destroy_listener);
		state->buffer_destroy_listener.notify = buffer_destroy;
	}
}

static void surface_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *buffer, int32_t sx, int32_t sy) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);

	surface->pending->invalid |= WLR_SURFACE_INVALID_BUFFER;
	surface->pending->sx = sx;
	surface->pending->sy = sy;
	wlr_surface_state_reset_buffer(surface->pending);
	wlr_surface_state_set_buffer(surface->pending, buffer);
}

static void surface_damage(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending->invalid |= WLR_SURFACE_INVALID_SURFACE_DAMAGE;
	pixman_region32_union_rect(&surface->pending->surface_damage,
		&surface->pending->surface_damage,
		x, y, width, height);
}

static void destroy_frame_callback(struct wl_resource *resource) {
	struct wlr_frame_callback *cb = wl_resource_get_user_data(resource);
	wl_list_remove(&cb->link);
	free(cb);
}

static void surface_frame(struct wl_client *client,
		struct wl_resource *resource, uint32_t callback) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);

	struct wlr_frame_callback *cb =
		calloc(1, sizeof(struct wlr_frame_callback));
	if (cb == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	cb->resource = wl_resource_create(client, &wl_callback_interface, 1,
		callback);
	if (cb->resource == NULL) {
		free(cb);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(cb->resource,
		NULL, cb, destroy_frame_callback);

	wl_list_insert(surface->pending->frame_callback_list.prev, &cb->link);

	surface->pending->invalid |= WLR_SURFACE_INVALID_FRAME_CALLBACK_LIST;
}

static void surface_set_opaque_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if ((surface->pending->invalid & WLR_SURFACE_INVALID_OPAQUE_REGION)) {
		pixman_region32_clear(&surface->pending->opaque);
	}
	surface->pending->invalid |= WLR_SURFACE_INVALID_OPAQUE_REGION;
	if (region_resource) {
		pixman_region32_t *region = wl_resource_get_user_data(region_resource);
		pixman_region32_copy(&surface->pending->opaque, region);
	} else {
		pixman_region32_clear(&surface->pending->opaque);
	}
}

static void surface_set_input_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->pending->invalid |= WLR_SURFACE_INVALID_INPUT_REGION;
	if (region_resource) {
		pixman_region32_t *region = wl_resource_get_user_data(region_resource);
		pixman_region32_copy(&surface->pending->input, region);
	} else {
		pixman_region32_init_rect(&surface->pending->input,
			INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
	}
}

static bool wlr_surface_update_size(struct wlr_surface *surface,
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

	wlr_texture_get_buffer_size(surface->texture, state->buffer,
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

	struct wlr_frame_callback *cb, *tmp;
	wl_list_for_each_safe(cb, tmp, &state->frame_callback_list, link) {
		wl_resource_destroy(cb->resource);
	}
	wl_list_init(&state->frame_callback_list);

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
static void wlr_surface_move_state(struct wlr_surface *surface,
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
		wlr_surface_state_release_buffer(state);
		wlr_surface_state_set_buffer(state, next->buffer);
		wlr_surface_state_reset_buffer(next);
		state->sx = next->sx;
		state->sy = next->sy;
		update_size = true;
	}
	if (update_size) {
		update_damage = wlr_surface_update_size(surface, state);
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

static void wlr_surface_damage_subsurfaces(struct wlr_subsurface *subsurface) {
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
	wl_list_for_each(child, &subsurface->surface->subsurface_list, parent_link) {
		wlr_surface_damage_subsurfaces(child);
	}
}

static void wlr_surface_apply_damage(struct wlr_surface *surface,
		bool reupload_buffer) {
	if (!surface->current->buffer) {
		return;
	}
	struct wl_shm_buffer *buffer = wl_shm_buffer_get(surface->current->buffer);
	if (!buffer) {
		if (wlr_renderer_buffer_is_drm(surface->renderer,
					surface->current->buffer)) {
			wlr_texture_upload_drm(surface->texture, surface->current->buffer);
			goto release;
		} else {
			wlr_log(L_INFO, "Unknown buffer handle attached");
			return;
		}
	}

	uint32_t format = wl_shm_buffer_get_format(buffer);
	if (reupload_buffer) {
		wlr_texture_upload_shm(surface->texture, format, buffer);
	} else {
		pixman_region32_t damage;
		pixman_region32_init(&damage);
		pixman_region32_copy(&damage, &surface->current->buffer_damage);
		pixman_region32_intersect_rect(&damage, &damage, 0, 0,
			surface->current->buffer_width, surface->current->buffer_height);

		int n;
		pixman_box32_t *rects = pixman_region32_rectangles(&damage, &n);
		for (int i = 0; i < n; ++i) {
			pixman_box32_t rect = rects[i];
			if (!wlr_texture_update_shm(surface->texture, format,
					rect.x1, rect.y1,
					rect.x2 - rect.x1,
					rect.y2 - rect.y1,
					buffer)) {
				break;
			}
		}

		pixman_region32_fini(&damage);
	}

release:
	wlr_surface_state_release_buffer(surface->current);
}

static void wlr_surface_commit_pending(struct wlr_surface *surface) {
	int32_t oldw = surface->current->buffer_width;
	int32_t oldh = surface->current->buffer_height;

	bool null_buffer_commit =
		(surface->pending->invalid & WLR_SURFACE_INVALID_BUFFER &&
		 surface->pending->buffer == NULL);

	wlr_surface_move_state(surface, surface->pending, surface->current);

	if (null_buffer_commit) {
		surface->texture->valid = false;
	}

	bool reupload_buffer = oldw != surface->current->buffer_width ||
		oldh != surface->current->buffer_height;
	wlr_surface_apply_damage(surface, reupload_buffer);

	// commit subsurface order
	struct wlr_subsurface *subsurface;
	wl_list_for_each_reverse(subsurface, &surface->subsurface_pending_list,
			parent_pending_link) {
		wl_list_remove(&subsurface->parent_link);
		wl_list_insert(&surface->subsurface_list, &subsurface->parent_link);

		if (subsurface->reordered) {
			// TODO: damage all the subsurfaces
			wlr_surface_damage_subsurfaces(subsurface);
		}
	}

	if (surface->role_committed) {
		surface->role_committed(surface, surface->role_data);
	}

	// TODO: add the invalid bitfield to this callback
	wl_signal_emit(&surface->events.commit, surface);

	pixman_region32_clear(&surface->current->surface_damage);
	pixman_region32_clear(&surface->current->buffer_damage);
}

static bool wlr_subsurface_is_synchronized(struct wlr_subsurface *subsurface) {
	while (subsurface) {
		if (subsurface->synchronized) {
			return true;
		}

		if (!subsurface->parent) {
			return false;
		}

		subsurface = subsurface->parent->subsurface;
	}

	return false;
}

/**
 * Recursive function to commit the effectively synchronized children.
 */
static void wlr_subsurface_parent_commit(struct wlr_subsurface *subsurface,
		bool synchronized) {
		struct wlr_surface *surface = subsurface->surface;
	if (synchronized || subsurface->synchronized) {
		if (subsurface->has_cache) {
			wlr_surface_move_state(surface, subsurface->cached, surface->pending);
			wlr_surface_commit_pending(surface);
			subsurface->has_cache = false;
			subsurface->cached->invalid = 0;
		}

		struct wlr_subsurface *tmp;
		wl_list_for_each(tmp, &surface->subsurface_list, parent_link) {
			wlr_subsurface_parent_commit(tmp, true);
		}
	}
}

static void wlr_subsurface_commit(struct wlr_subsurface *subsurface) {
	struct wlr_surface *surface = subsurface->surface;

	if (wlr_subsurface_is_synchronized(subsurface)) {
		wlr_surface_move_state(surface, surface->pending, subsurface->cached);
		subsurface->has_cache = true;
	} else {
		if (subsurface->has_cache) {
			wlr_surface_move_state(surface, subsurface->cached, surface->pending);
			wlr_surface_commit_pending(surface);
			subsurface->has_cache = false;

		} else {
			wlr_surface_commit_pending(surface);
		}

		struct wlr_subsurface *tmp;
		wl_list_for_each(tmp, &surface->subsurface_list, parent_link) {
			wlr_subsurface_parent_commit(tmp, false);
		}
	}

}

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_subsurface *subsurface = surface->subsurface;

	if (subsurface) {
		wlr_subsurface_commit(subsurface);
		return;
	}

	wlr_surface_commit_pending(surface);

	struct wlr_subsurface *tmp;
	wl_list_for_each(tmp, &surface->subsurface_list, parent_link) {
		wlr_subsurface_parent_commit(tmp, false);
	}
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->pending->invalid |= WLR_SURFACE_INVALID_TRANSFORM;
	surface->pending->transform = transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->pending->invalid |= WLR_SURFACE_INVALID_SCALE;
	surface->pending->scale = scale;
}

static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending->invalid |= WLR_SURFACE_INVALID_BUFFER_DAMAGE;
	pixman_region32_union_rect(&surface->pending->buffer_damage,
		&surface->pending->buffer_damage,
		x, y, width, height);
}

const struct wl_surface_interface surface_interface = {
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

static struct wlr_surface_state *wlr_surface_state_create() {
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

static void wlr_surface_state_destroy(struct wlr_surface_state *state) {
	wlr_surface_state_reset_buffer(state);
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

void wlr_subsurface_destroy(struct wlr_subsurface *subsurface) {
	wl_signal_emit(&subsurface->events.destroy, subsurface);

	wlr_surface_state_destroy(subsurface->cached);

	if (subsurface->parent) {
		wl_list_remove(&subsurface->parent_link);
		wl_list_remove(&subsurface->parent_pending_link);
		wl_list_remove(&subsurface->parent_destroy_listener.link);
	}

	wl_resource_set_user_data(subsurface->resource, NULL);
	if (subsurface->surface) {
		subsurface->surface->subsurface = NULL;
	}
	free(subsurface);
}

static void destroy_surface(struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	wl_signal_emit(&surface->events.destroy, surface);

	if (surface->subsurface) {
		wlr_subsurface_destroy(surface->subsurface);
	}

	wlr_texture_destroy(surface->texture);
	wlr_surface_state_destroy(surface->pending);
	wlr_surface_state_destroy(surface->current);

	free(surface);
}

struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer) {
	struct wlr_surface *surface = calloc(1, sizeof(struct wlr_surface));
	if (!surface) {
		wl_resource_post_no_memory(res);
		return NULL;
	}
	wlr_log(L_DEBUG, "New wlr_surface %p (res %p)", surface, res);
	surface->renderer = renderer;
	surface->texture = wlr_render_texture_create(renderer);
	surface->resource = res;

	surface->current = wlr_surface_state_create();
	surface->pending = wlr_surface_state_create();

	wl_signal_init(&surface->events.commit);
	wl_signal_init(&surface->events.destroy);
	wl_signal_init(&surface->events.new_subsurface);
	wl_list_init(&surface->subsurface_list);
	wl_list_init(&surface->subsurface_pending_list);
	wl_resource_set_implementation(res, &surface_interface,
		surface, destroy_surface);
	return surface;
}

void wlr_surface_get_matrix(struct wlr_surface *surface,
		float (*matrix)[16],
		const float (*projection)[16],
		const float (*transform)[16]) {
	int width = surface->texture->width;
	int height = surface->texture->height;
	float scale[16];
	wlr_matrix_identity(matrix);
	if (transform) {
		wlr_matrix_mul(matrix, transform, matrix);
	}
	wlr_matrix_scale(&scale, width, height, 1);
	wlr_matrix_mul(matrix, &scale, matrix);
	wlr_matrix_mul(projection, matrix, matrix);
}

bool wlr_surface_has_buffer(struct wlr_surface *surface) {
	return surface->texture && surface->texture->valid;
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

static void subsurface_resource_destroy(struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = wl_resource_get_user_data(resource);

	if (subsurface) {
		wlr_subsurface_destroy(subsurface);
	}
}

static void subsurface_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void subsurface_set_position(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_subsurface *subsurface = wl_resource_get_user_data(resource);
	struct wlr_surface *surface = subsurface->surface;

	surface->pending->invalid |= WLR_SURFACE_INVALID_SUBSURFACE_POSITION;

	surface->pending->subsurface_position.x = x;
	surface->pending->subsurface_position.y = y;
}

static struct wlr_subsurface *subsurface_find_sibling(
		struct wlr_subsurface *subsurface, struct wlr_surface *surface) {
	struct wlr_surface *parent = subsurface->parent;

	struct wlr_subsurface *sibling;
	wl_list_for_each(sibling, &parent->subsurface_list, parent_link) {
		if (sibling->surface == surface && sibling != subsurface) {
			return sibling;
		}
	}

	return NULL;
}

static void subsurface_place_above(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling_resource) {
	struct wlr_subsurface *subsurface = wl_resource_get_user_data(resource);

	struct wlr_surface *sibling_surface =
		wl_resource_get_user_data(sibling_resource);
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
	wl_list_insert(sibling->parent_pending_link.prev,
		&subsurface->parent_pending_link);

	subsurface->reordered = true;
}

static void subsurface_place_below(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling_resource) {
	struct wlr_subsurface *subsurface = wl_resource_get_user_data(resource);

	struct wlr_surface *sibling_surface =
		wl_resource_get_user_data(sibling_resource);
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
	wl_list_insert(&sibling->parent_pending_link,
		&subsurface->parent_pending_link);

	subsurface->reordered = true;
}

static void subsurface_set_sync(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = wl_resource_get_user_data(resource);

	if (subsurface) {
		subsurface->synchronized = true;
	}
}

static void subsurface_set_desync(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_subsurface *subsurface = wl_resource_get_user_data(resource);

	if (subsurface && subsurface->synchronized) {
		subsurface->synchronized = false;

		if (!wlr_subsurface_is_synchronized(subsurface)) {
			// TODO: do a synchronized commit to flush the cache
			wlr_subsurface_parent_commit(subsurface, true);
		}
	}
}

static const struct wl_subsurface_interface subsurface_implementation = {
	.destroy = subsurface_destroy,
	.set_position = subsurface_set_position,
	.place_above = subsurface_place_above,
	.place_below = subsurface_place_below,
	.set_sync = subsurface_set_sync,
	.set_desync = subsurface_set_desync,
};

static void subsurface_handle_parent_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_subsurface *subsurface =
		wl_container_of(listener, subsurface, parent_destroy_listener);
	wl_list_remove(&subsurface->parent_link);
	wl_list_remove(&subsurface->parent_pending_link);
	wl_list_remove(&subsurface->parent_destroy_listener.link);
	subsurface->parent = NULL;
}

void wlr_surface_make_subsurface(struct wlr_surface *surface,
		struct wlr_surface *parent, uint32_t id) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	assert(surface->subsurface == NULL);

	struct wlr_subsurface *subsurface =
		calloc(1, sizeof(struct wlr_subsurface));
	if (!subsurface) {
		wl_client_post_no_memory(client);
		return;
	}
	subsurface->cached = wlr_surface_state_create();
	if (subsurface->cached == NULL) {
		free(subsurface);
		wl_client_post_no_memory(client);
		return;
	}
	subsurface->synchronized = true;
	subsurface->surface = surface;
	wl_signal_init(&subsurface->events.destroy);

	// link parent
	subsurface->parent = parent;
	wl_signal_add(&parent->events.destroy,
		&subsurface->parent_destroy_listener);
	subsurface->parent_destroy_listener.notify =
		subsurface_handle_parent_destroy;
	wl_list_insert(&parent->subsurface_list, &subsurface->parent_link);
	wl_list_insert(&parent->subsurface_pending_list,
		&subsurface->parent_pending_link);

	subsurface->resource =
		wl_resource_create(client, &wl_subsurface_interface, 1, id);
	if (subsurface->resource == NULL) {
		wlr_surface_state_destroy(subsurface->cached);
		free(subsurface);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(subsurface->resource,
		&subsurface_implementation, subsurface,
		subsurface_resource_destroy);

	surface->subsurface = subsurface;

	wl_signal_emit(&parent->events.new_subsurface, subsurface);
}


struct wlr_surface *wlr_surface_get_main_surface(struct wlr_surface *surface) {
	struct wlr_subsurface *sub;

	while (surface && (sub = surface->subsurface)) {
		surface = sub->parent;
	}

	return surface;
}

struct wlr_subsurface *wlr_surface_subsurface_at(struct wlr_surface *surface,
		double sx, double sy, double *sub_x, double *sub_y) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		double _sub_x = subsurface->surface->current->subsurface_position.x;
		double _sub_y = subsurface->surface->current->subsurface_position.y;
		struct wlr_subsurface *sub =
			wlr_surface_subsurface_at(subsurface->surface, _sub_x + sx,
				_sub_y + sy, sub_x, sub_y);
		if (sub) {
			// TODO: This won't work for nested subsurfaces. Convert sub_x and
			// sub_y to the parent coordinate system
			return sub;
		}

		int sub_width = subsurface->surface->current->buffer_width;
		int sub_height = subsurface->surface->current->buffer_height;
		if ((sx > _sub_x && sx < _sub_x + sub_width) &&
				(sy > _sub_y && sy < _sub_y + sub_height)) {
			if (pixman_region32_contains_point(
						&subsurface->surface->current->input,
						sx - _sub_x, sy - _sub_y, NULL)) {
				*sub_x = _sub_x;
				*sub_y = _sub_y;
				return subsurface;
			}
		}
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
			break;
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
			break;
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
