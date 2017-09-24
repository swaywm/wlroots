#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/egl.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/render/matrix.h>

static void surface_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *buffer, int32_t sx, int32_t sy) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->pending.invalid |= WLR_SURFACE_INVALID_BUFFER;
	surface->pending.buffer = buffer;
}

static void surface_damage(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending.invalid |= WLR_SURFACE_INVALID_SURFACE_DAMAGE;
	pixman_region32_union_rect(&surface->pending.surface_damage,
		&surface->pending.surface_damage,
		x, y, width, height);
}

static void destroy_frame_callback(struct wl_resource *resource) {
	struct wlr_frame_callback *cb = wl_resource_get_user_data(resource);
	wl_list_remove(&cb->link);
	free(cb);
}

static void surface_frame(struct wl_client *client,
		struct wl_resource *resource, uint32_t callback) {
	struct wlr_frame_callback *cb;
	struct wlr_surface *surface = wl_resource_get_user_data(resource);

	cb = malloc(sizeof(struct wlr_frame_callback));
	if (cb == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	cb->resource = wl_resource_create(client,
			&wl_callback_interface, 1, callback);
	if (cb->resource == NULL) {
		free(cb);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(cb->resource,
		NULL, cb, destroy_frame_callback);

	wl_list_insert(surface->frame_callback_list.prev, &cb->link);
}

static void surface_set_opaque_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if ((surface->pending.invalid & WLR_SURFACE_INVALID_OPAQUE_REGION)) {
		pixman_region32_clear(&surface->pending.opaque);
	}
	surface->pending.invalid |= WLR_SURFACE_INVALID_OPAQUE_REGION;
	if (region_resource) {
		pixman_region32_t *region = wl_resource_get_user_data(region_resource);
		pixman_region32_copy(&surface->pending.opaque, region);
	} else {
		pixman_region32_clear(&surface->pending.opaque);
	}
}

static void surface_set_input_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if ((surface->pending.invalid & WLR_SURFACE_INVALID_INPUT_REGION)) {
		pixman_region32_clear(&surface->pending.input);
	}
	surface->pending.invalid |= WLR_SURFACE_INVALID_INPUT_REGION;
	if (region_resource) {
		pixman_region32_t *region = wl_resource_get_user_data(region_resource);
		pixman_region32_copy(&surface->pending.input, region);
	} else {
		pixman_region32_init_rect(&surface->pending.input,
			INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
	}
}

static void wlr_surface_update_size(struct wlr_surface *surface) {
	int scale = surface->current.scale;
	enum wl_output_transform transform = surface->current.transform;

	wlr_texture_get_buffer_size(surface->texture, surface->current.buffer,
		&surface->current.buffer_width, &surface->current.buffer_height);

	int _width = surface->current.buffer_width / scale;
	int _height = surface->current.buffer_height / scale;

	if (transform == WL_OUTPUT_TRANSFORM_90 ||
		transform == WL_OUTPUT_TRANSFORM_270 ||
		transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
		transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
		int tmp = _width;
		_width = _height;
		_height = tmp;
	}

	surface->current.width = _width;
	surface->current.height = _height;
}

static void wlr_surface_to_buffer_region(struct wlr_surface *surface,
		pixman_region32_t *surface_region, pixman_region32_t *buffer_region,
		int width, int height) {
	pixman_box32_t *src_rects, *dest_rects;
	int nrects, i;
	int scale = surface->current.scale;
	enum wl_output_transform transform = surface->current.transform;

	src_rects = pixman_region32_rectangles(surface_region, &nrects);
	dest_rects = malloc(nrects * sizeof(*dest_rects));
	if (!dest_rects) {
		return;
	}

	for (i = 0; i < nrects; i++) {
		switch (transform) {
		default:
		case WL_OUTPUT_TRANSFORM_NORMAL:
			dest_rects[i].x1 = src_rects[i].x1;
			dest_rects[i].y1 = src_rects[i].y1;
			dest_rects[i].x2 = src_rects[i].x2;
			dest_rects[i].y2 = src_rects[i].y2;
			break;
		case WL_OUTPUT_TRANSFORM_90:
			dest_rects[i].x1 = height - src_rects[i].y2;
			dest_rects[i].y1 = src_rects[i].x1;
			dest_rects[i].x2 = height - src_rects[i].y1;
			dest_rects[i].y2 = src_rects[i].x2;
			break;
		case WL_OUTPUT_TRANSFORM_180:
			dest_rects[i].x1 = width - src_rects[i].x2;
			dest_rects[i].y1 = height - src_rects[i].y2;
			dest_rects[i].x2 = width - src_rects[i].x1;
			dest_rects[i].y2 = height - src_rects[i].y1;
			break;
		case WL_OUTPUT_TRANSFORM_270:
			dest_rects[i].x1 = src_rects[i].y1;
			dest_rects[i].y1 = width - src_rects[i].x2;
			dest_rects[i].x2 = src_rects[i].y2;
			dest_rects[i].y2 = width - src_rects[i].x1;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED:
			dest_rects[i].x1 = width - src_rects[i].x2;
			dest_rects[i].y1 = src_rects[i].y1;
			dest_rects[i].x2 = width - src_rects[i].x1;
			dest_rects[i].y2 = src_rects[i].y2;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_90:
			dest_rects[i].x1 = height - src_rects[i].y2;
			dest_rects[i].y1 = width - src_rects[i].x2;
			dest_rects[i].x2 = height - src_rects[i].y1;
			dest_rects[i].y2 = width - src_rects[i].x1;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_180:
			dest_rects[i].x1 = src_rects[i].x1;
			dest_rects[i].y1 = height - src_rects[i].y2;
			dest_rects[i].x2 = src_rects[i].x2;
			dest_rects[i].y2 = height - src_rects[i].y1;
			break;
		case WL_OUTPUT_TRANSFORM_FLIPPED_270:
			dest_rects[i].x1 = src_rects[i].y1;
			dest_rects[i].y1 = src_rects[i].x1;
			dest_rects[i].x2 = src_rects[i].y2;
			dest_rects[i].y2 = src_rects[i].x2;
			break;
		}
	}

	if (scale != 1) {
		for (i = 0; i < nrects; i++) {
			dest_rects[i].x1 *= scale;
			dest_rects[i].x2 *= scale;
			dest_rects[i].y1 *= scale;
			dest_rects[i].y2 *= scale;
		}
	}

	pixman_region32_fini(buffer_region);
	pixman_region32_init_rects(buffer_region, dest_rects, nrects);
	free(dest_rects);
}

static void wlr_surface_commit_state(struct wlr_surface *surface,
		struct wlr_surface_state *pending) {
	bool update_size = false;
	bool update_damage = false;

	if ((pending->invalid & WLR_SURFACE_INVALID_SCALE)) {
		surface->current.scale = pending->scale;
		update_size = true;
	}
	if ((pending->invalid & WLR_SURFACE_INVALID_TRANSFORM)) {
		surface->current.transform = pending->transform;
		update_size = true;
	}
	if ((pending->invalid & WLR_SURFACE_INVALID_BUFFER)) {
		surface->current.buffer = pending->buffer;
		update_size = true;
	}
	if (update_size) {
		int32_t oldw = surface->current.buffer_width;
		int32_t oldh = surface->current.buffer_height;
		wlr_surface_update_size(surface);

		surface->reupload_buffer = oldw != surface->current.buffer_width ||
			oldh != surface->current.buffer_height;
	}
	if ((pending->invalid & WLR_SURFACE_INVALID_SURFACE_DAMAGE)) {
		pixman_region32_union(&surface->current.surface_damage,
			&surface->current.surface_damage,
			&pending->surface_damage);
		pixman_region32_intersect_rect(&surface->current.surface_damage,
			&surface->current.surface_damage, 0, 0, surface->current.width,
			surface->current.height);

		pixman_region32_clear(&pending->surface_damage);
		update_damage = true;
	}
	if ((pending->invalid & WLR_SURFACE_INVALID_BUFFER_DAMAGE)) {
		pixman_region32_union(&surface->current.buffer_damage,
			&surface->current.buffer_damage,
			&pending->buffer_damage);

		pixman_region32_clear(&pending->buffer_damage);
		update_damage = true;
	}
	if (update_damage) {
		pixman_region32_t buffer_damage;
		pixman_region32_init(&buffer_damage);
		wlr_surface_to_buffer_region(surface, &surface->current.surface_damage,
			&buffer_damage, surface->current.width, surface->current.height);
		pixman_region32_union(&surface->current.buffer_damage,
			&surface->current.buffer_damage, &buffer_damage);
		pixman_region32_fini(&buffer_damage);

		pixman_region32_intersect_rect(&surface->current.buffer_damage,
			&surface->current.buffer_damage, 0, 0,
			surface->current.buffer_width, surface->current.buffer_height);
	}
	if ((pending->invalid & WLR_SURFACE_INVALID_OPAQUE_REGION)) {
		// TODO: process buffer
		pixman_region32_clear(&pending->opaque);
	}
	if ((pending->invalid & WLR_SURFACE_INVALID_INPUT_REGION)) {
		// TODO: process buffer
		pixman_region32_clear(&pending->input);
	}

	pending->invalid = 0;
	// TODO: add the invalid bitfield to this callback
	wl_signal_emit(&surface->signals.commit, surface);
}

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);

	wlr_surface_commit_state(surface, &surface->pending);
}

void wlr_surface_flush_damage(struct wlr_surface *surface) {
	if (!surface->current.buffer) {
		if (surface->texture->valid) {
			// TODO: Detach buffers
		}
		return;
	}
	struct wl_shm_buffer *buffer = wl_shm_buffer_get(surface->current.buffer);
	if (!buffer) {
		if (wlr_renderer_buffer_is_drm(surface->renderer,
					surface->pending.buffer)) {
			wlr_texture_upload_drm(surface->texture, surface->pending.buffer);
			goto release;
		} else {
			wlr_log(L_INFO, "Unknown buffer handle attached");
			return;
		}
	}

	uint32_t format = wl_shm_buffer_get_format(buffer);
	if (surface->reupload_buffer) {
		wlr_texture_upload_shm(surface->texture, format, buffer);
	} else {
		pixman_region32_t damage = surface->current.buffer_damage;
		if (!pixman_region32_not_empty(&damage)) {
			goto release;
		}
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
	}

release:
	pixman_region32_clear(&surface->current.surface_damage);
	pixman_region32_clear(&surface->current.buffer_damage);

	wl_resource_post_event(surface->current.buffer, WL_BUFFER_RELEASE);
	surface->current.buffer = NULL;
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->pending.invalid |= WLR_SURFACE_INVALID_TRANSFORM;
	surface->pending.transform = transform;
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->pending.invalid |= WLR_SURFACE_INVALID_SCALE;
	surface->pending.scale = scale;
}

static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	if (width < 0 || height < 0) {
		return;
	}
	surface->pending.invalid |= WLR_SURFACE_INVALID_BUFFER_DAMAGE;
	pixman_region32_union_rect(&surface->pending.buffer_damage,
		&surface->pending.buffer_damage,
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

static void destroy_surface(struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	wl_signal_emit(&surface->signals.destroy, surface);

	wlr_texture_destroy(surface->texture);
	struct wlr_frame_callback *cb, *next;
	wl_list_for_each_safe(cb, next, &surface->frame_callback_list, link) {
		wl_resource_destroy(cb->resource);
	}
	pixman_region32_fini(&surface->pending.surface_damage);
	pixman_region32_fini(&surface->pending.buffer_damage);
	pixman_region32_fini(&surface->pending.opaque);
	pixman_region32_fini(&surface->pending.input);

	free(surface);
}

void wlr_surface_state_init(struct wlr_surface_state *state) {
	state->scale = 1;
	state->transform = WL_OUTPUT_TRANSFORM_NORMAL;

	pixman_region32_init(&state->surface_damage);
	pixman_region32_init(&state->buffer_damage);
	pixman_region32_init(&state->opaque);
	pixman_region32_init(&state->input);
}

struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer) {
	struct wlr_surface *surface;
	if (!(surface = calloc(1, sizeof(struct wlr_surface)))) {
		wl_resource_post_no_memory(res);
		return NULL;
	}
	wlr_log(L_DEBUG, "New wlr_surface %p (res %p)", surface, res);
	surface->renderer = renderer;
	surface->texture = wlr_render_texture_create(renderer);
	surface->resource = res;

	wlr_surface_state_init(&surface->current);
	wlr_surface_state_init(&surface->pending);

	wl_signal_init(&surface->signals.commit);
	wl_signal_init(&surface->signals.destroy);
	wl_list_init(&surface->frame_callback_list);
	wl_resource_set_implementation(res, &surface_interface,
		surface, destroy_surface);
	return surface;
}

void wlr_surface_get_matrix(struct wlr_surface *surface,
		float (*matrix)[16],
		const float (*projection)[16],
		const float (*transform)[16]) {
	int width = surface->texture->width / surface->current.scale;
	int height = surface->texture->height / surface->current.scale;
	float scale[16];
	wlr_matrix_identity(matrix);
	if (transform) {
		wlr_matrix_mul(matrix, transform, matrix);
	}
	wlr_matrix_scale(&scale, width, height, 1);
	wlr_matrix_mul(matrix, &scale, matrix);
	wlr_matrix_mul(projection, matrix, matrix);
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

void wlr_subsurface_destroy(struct wlr_subsurface *subsurface) {
	wlr_log(L_DEBUG, "TODO: wlr subsurface destroy");
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
	subsurface->pending_position.set = true;
	subsurface->pending_position.x = x;
	subsurface->pending_position.y = y;
}

static void subsurface_place_above(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling) {
	wlr_log(L_DEBUG, "TODO: subsurface place above");
}

static void subsurface_place_below(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *sibling) {
	wlr_log(L_DEBUG, "TODO: subsurface place below");
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

void wlr_surface_make_subsurface(struct wlr_surface *surface,
		struct wlr_surface *parent, uint32_t id) {
	assert(surface->subsurface == NULL);

	struct wlr_subsurface *subsurface =
		calloc(1, sizeof(struct wlr_subsurface));
	if (!subsurface) {
		return;
	}

	subsurface->surface = surface;
	subsurface->parent = parent;

	struct wl_client *client = wl_resource_get_client(surface->resource);

	subsurface->resource =
		wl_resource_create(client, &wl_subsurface_interface, 1, id);

	wl_resource_set_implementation(subsurface->resource,
		&subsurface_implementation, subsurface,
		subsurface_resource_destroy);

	surface->subsurface = subsurface;
}
