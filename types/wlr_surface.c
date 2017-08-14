#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/egl.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_surface.h>

static void surface_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *buffer, int32_t sx, int32_t sy) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	int scale = surface->current.scale;
	surface->pending.invalid |= WLR_SURFACE_INVALID_BUFFER;
	surface->pending.buffer = buffer;
	surface->texture->height_from_buffer = surface->texture->height / scale;
	surface->texture->width_from_buffer = surface->texture->width / scale;
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
	surface->pending.invalid |= WLR_SURFACE_INVALID_INPUT_REGION;
	if (region_resource) {
		pixman_region32_t *region = wl_resource_get_user_data(region_resource);
		pixman_region32_copy(&surface->pending.input, region);
	} else {
		pixman_region32_fini(&surface->pending.input);
		pixman_region32_init_rect(&surface->pending.input,
				INT32_MIN, INT32_MIN, UINT32_MAX, UINT32_MAX);
	}
}

static void wlr_surface_to_buffer_region(struct wlr_surface *surface,
		pixman_region32_t *surface_region, pixman_region32_t *buffer_region) {
	pixman_box32_t *src_rects, *dest_rects;
	int nrects, i;
	int scale = surface->current.scale;

	src_rects = pixman_region32_rectangles(surface_region, &nrects);
	dest_rects = malloc(nrects * sizeof(*dest_rects));
	if (!dest_rects) {
		return;
	}

	for (i = 0; i < nrects; i++) {
		dest_rects[i].x1 = src_rects[i].x1 * scale;
		dest_rects[i].y1 = src_rects[i].y1 * scale;
		dest_rects[i].x2 = src_rects[i].x2 * scale;
		dest_rects[i].y2 = src_rects[i].y2 * scale;
	}

	pixman_region32_fini(buffer_region);
	pixman_region32_init_rects(buffer_region, dest_rects, nrects);
	free(dest_rects);
}

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
	surface->current.scale = surface->pending.scale;

	if ((surface->pending.invalid & WLR_SURFACE_INVALID_BUFFER)) {
		surface->current.buffer = surface->pending.buffer;
	}
	if ((surface->pending.invalid & WLR_SURFACE_INVALID_SURFACE_DAMAGE)) {
		int width, height;
		wlr_texture_get_buffer_size(surface->texture, surface->current.buffer, &width, &height);

		pixman_region32_union(&surface->current.surface_damage,
				&surface->current.surface_damage,
				&surface->pending.surface_damage);
		pixman_region32_intersect_rect(&surface->current.surface_damage,
			&surface->current.surface_damage, 0, 0, width /
			surface->current.scale, height / surface->current.scale);

		pixman_region32_union(&surface->current.buffer_damage,
			&surface->current.buffer_damage,
			&surface->pending.buffer_damage);

		pixman_region32_t buffer_damage;
		pixman_region32_init(&buffer_damage);
		wlr_surface_to_buffer_region(surface, &surface->current.surface_damage,
			&buffer_damage);
		pixman_region32_union(&surface->current.buffer_damage,
			&surface->current.buffer_damage, &buffer_damage);

		pixman_region32_intersect_rect(&surface->current.buffer_damage,
			&surface->current.buffer_damage, 0, 0, width, height);

		pixman_region32_clear(&surface->pending.surface_damage);
		pixman_region32_clear(&surface->pending.buffer_damage);
	}
	// TODO: Commit other changes

	surface->pending.invalid = 0;
	// TODO: add the invalid bitfield to this callback
	wl_signal_emit(&surface->signals.commit, surface);
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
		if (wlr_renderer_buffer_is_drm(surface->renderer, surface->pending.buffer)) {
			wlr_texture_upload_drm(surface->texture, surface->pending.buffer);
			goto release;
		} else {
			wlr_log(L_INFO, "Unknown buffer handle attached");
			return;
		}
	}
	pixman_region32_t damage = surface->current.buffer_damage;
	if (!pixman_region32_not_empty(&damage)) {
		goto release;
	}
	int n;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &n);
	uint32_t format = wl_shm_buffer_get_format(buffer);
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
	pixman_region32_fini(&surface->current.surface_damage);
	pixman_region32_init(&surface->current.surface_damage);

	pixman_region32_fini(&surface->current.buffer_damage);
	pixman_region32_init(&surface->current.buffer_damage);
release:
	wl_resource_queue_event(surface->current.buffer, WL_BUFFER_RELEASE);
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	wlr_log(L_DEBUG, "TODO: surface surface buffer transform");
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);
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
	surface->pending.invalid |= WLR_SURFACE_INVALID_SURFACE_DAMAGE;
	pixman_region32_union_rect(&surface->pending.buffer_damage,
		&surface->pending.buffer_damage,
		x, y, width, height);
}

const struct wl_surface_interface surface_interface = {
	surface_destroy,
	surface_attach,
	surface_damage,
	surface_frame,
	surface_set_opaque_region,
	surface_set_input_region,
	surface_commit,
	surface_set_buffer_transform,
	surface_set_buffer_scale,
	surface_damage_buffer
};

static void destroy_surface(struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);

	wlr_texture_destroy(surface->texture);
	struct wlr_frame_callback *cb, *next;
	wl_list_for_each_safe(cb, next, &surface->frame_callback_list, link) {
		wl_resource_destroy(cb->resource);
	}

	free(surface);
}

struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer) {
	struct wlr_surface *surface = calloc(1, sizeof(struct wlr_surface));
	surface->renderer = renderer;
	surface->texture = wlr_render_texture_init(renderer);
	surface->resource = res;
	surface->current.scale = 1;
	surface->pending.scale = 1;
	wl_signal_init(&surface->signals.commit);
	wl_list_init(&surface->frame_callback_list);
	wl_resource_set_implementation(res, &surface_interface,
			surface, destroy_surface);
	return surface;
}
