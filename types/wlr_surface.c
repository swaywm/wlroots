#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_surface.h>

static void surface_destroy(struct wl_client *client, struct wl_resource *resource) {
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

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(resource);

	if ((surface->pending.invalid & WLR_SURFACE_INVALID_BUFFER)) {
		surface->current.buffer = surface->pending.buffer;
		// TODO: Move to wlr_surface_flush_damage and call from output frame
		// callbacks instead of immediately here
		if (surface->current.buffer) {
			struct wl_shm_buffer *buffer = wl_shm_buffer_get(surface->current.buffer);
			if (buffer) {
				uint32_t format = wl_shm_buffer_get_format(buffer);
				wlr_texture_upload_shm(surface->texture, format, buffer);
				wl_resource_queue_event(surface->current.buffer, WL_BUFFER_RELEASE);
			} else if (wlr_texture_upload_drm(surface->texture, surface->pending.buffer)) {
				wl_resource_queue_event(surface->current.buffer, WL_BUFFER_RELEASE);
			} else {
				wlr_log(L_INFO, "Unknown buffer handle attached");
			}
		}
	}
	if ((surface->pending.invalid & WLR_SURFACE_INVALID_SURFACE_DAMAGE)) {
		// TODO: Sort out buffer damage too
		pixman_region32_union(&surface->current.surface_damage,
				&surface->current.surface_damage,
				&surface->pending.surface_damage);
		// TODO: Surface sizing is complicated
		//pixman_region32_intersect_rect(&surface->current.surface_damage,
		//		&surface->current.surface_damage,
		//		0, 0, surface->width, surface->height);
		pixman_region32_clear(&surface->pending.surface_damage);
	}
	// TODO: Commit other changes

	surface->pending.invalid = 0;
	// TODO: add the invalid bitfield to this callback
	wl_signal_emit(&surface->signals.commit, surface);
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	wlr_log(L_DEBUG, "TODO: surface surface buffer transform");
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	wlr_log(L_DEBUG, "TODO: surface set buffer scale");
}

static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width,
		int32_t height) {
	wlr_log(L_DEBUG, "TODO: surface damage buffer");
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
	wl_signal_emit(&surface->signals.destroy, surface);
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
	surface->texture = wlr_render_texture_init(renderer);
	surface->resource = res;
	wl_signal_init(&surface->signals.commit);
	wl_signal_init(&surface->signals.destroy);
	wl_list_init(&surface->frame_callback_list);
	wl_resource_set_implementation(res, &surface_interface,
			surface, destroy_surface);
	return surface;
}
