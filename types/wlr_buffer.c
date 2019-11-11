#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/log.h>

struct wlr_buffer_impl_registration {
	const struct wlr_buffer_impl *impl;
	struct wl_list link;
};

static struct wl_list buffer_impls = {0}; // wlr_buffer_impl_registration::link

void wlr_buffer_register_implementation(const struct wlr_buffer_impl *impl) {
	if (buffer_impls.prev == 0 && buffer_impls.next == 0) {
		wl_list_init(&buffer_impls);
	}

	struct wlr_buffer_impl_registration *registration;
	wl_list_for_each(registration, &buffer_impls, link) {
		if (registration->impl == impl) {
			return; /* no-op */
		}
	}

	registration = calloc(1, sizeof(struct wlr_buffer_impl_registration));
	registration->impl = impl;
	wl_list_insert(&buffer_impls, &registration->link);
}

bool wlr_resource_is_buffer(struct wl_resource *resource) {
	return strcmp(wl_resource_get_class(resource), wl_buffer_interface.name) == 0;
}

bool wlr_buffer_get_resource_size(struct wl_resource *resource,
		struct wlr_renderer *renderer, int *width, int *height) {
	assert(wlr_resource_is_buffer(resource));

	struct wlr_buffer_impl_registration *r;
	wl_list_for_each(r, &buffer_impls, link) {
		if (r->impl->is_instance(resource)) {
			if (!r->impl->get_resource_size) {
				*width = *height = 0;
				return false;
			}
			return r->impl->get_resource_size(
					resource, renderer, width, height);
		}
	}

	if (wlr_renderer_resource_is_wl_drm_buffer(renderer, resource)) {
		/* Special case */
		wlr_renderer_wl_drm_buffer_get_size(renderer, resource, width, height);
	} else {
		*width = *height = 0;
		return false;
	}

	return true;
}

static void buffer_resource_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_buffer *buffer =
		wl_container_of(listener, buffer, resource_destroy);
	wl_list_remove(&buffer->resource_destroy.link);
	wl_list_init(&buffer->resource_destroy.link);
	buffer->resource = NULL;

	// At this point, if the wl_buffer comes from linux-dmabuf or wl_drm, we
	// still haven't released it (ie. we'll read it in the future) but the
	// client destroyed it. Reading the texture itself should be fine because
	// we still hold a reference to the DMA-BUF via the texture. However the
	// client could decide to re-use the same DMA-BUF for something else, in
	// which case we'll read garbage. We decide to accept this risk.
}

struct wlr_buffer *wlr_buffer_create(struct wlr_renderer *renderer,
		struct wl_resource *resource) {
	assert(wlr_resource_is_buffer(resource));

	const struct wlr_buffer_impl *impl = NULL;
	struct wlr_buffer_impl_registration *registration;
	wl_list_for_each(registration, &buffer_impls, link) {
		if (registration->impl->is_instance(resource)) {
			impl = registration->impl;
			break;
		}
	}

	struct wlr_buffer *buffer = calloc(1, sizeof(struct wlr_buffer));
	if (buffer == NULL) {
		return NULL;
	}

	buffer->n_refs = 1;
	buffer->resource = resource;

	if (impl != NULL) {
		if (!impl->initialize(buffer, resource, renderer)) {
			free(buffer);
			return NULL;
		}
		buffer->impl = impl;
	} else if (wlr_renderer_resource_is_wl_drm_buffer(renderer, resource)) {
		/*
		 * Special case. This Wayland interface is implemented in mesa, so we
		 * need a renderer reference to determine if a type is a wl_drm
		 * instance. Instead of muddying the API for this one use-case, we just
		 * special-case wl_drm.
		 *
		 * wl_drm will eventually be deprecated anyway, so we can get rid of
		 * this branch in the future.
		 */
		buffer->texture = wlr_texture_from_wl_drm(renderer, resource);
	} else {
		wlr_log(WLR_ERROR, "Cannot upload texture: unknown buffer type");
		// Instead of just logging the error, also disconnect the client with a
		// fatal protocol error so that it's clear something went wrong.
		wl_resource_post_error(resource, 0, "unknown buffer type");
		free(buffer);
		return NULL;
	}

	wl_resource_add_destroy_listener(resource, &buffer->resource_destroy);
	buffer->resource_destroy.notify = buffer_resource_handle_destroy;
	return buffer;
}

struct wlr_buffer *wlr_buffer_ref(struct wlr_buffer *buffer) {
	buffer->n_refs++;
	return buffer;
}

void wlr_buffer_unref(struct wlr_buffer *buffer) {
	if (buffer == NULL) {
		return;
	}

	assert(buffer->n_refs > 0);
	buffer->n_refs--;
	if (buffer->n_refs > 0) {
		return;
	}

	if (!buffer->released && buffer->resource != NULL) {
		wl_buffer_send_release(buffer->resource);
	}

	if (buffer->impl && buffer->impl->destroy) {
		buffer->impl->destroy(buffer);
	}

	wl_list_remove(&buffer->resource_destroy.link);
	wlr_texture_destroy(buffer->texture);
	free(buffer);
}

struct wlr_buffer *wlr_buffer_apply_damage(struct wlr_buffer *buffer,
		struct wl_resource *resource, pixman_region32_t *damage) {
	assert(wlr_resource_is_buffer(resource));

	if (buffer->n_refs > 1) {
		/* Cannot update buffer with multiple references held */
		return NULL;
	}

	if (!buffer->impl || !buffer->impl->is_instance(resource)) {
		/* Cannot update from foreign buffer type */
		return NULL;
	}

	if (!buffer->impl->apply_damage) {
		return NULL;
	}

	if (!buffer->impl->apply_damage(buffer, resource, damage)) {
		return NULL;
	}

	wl_list_remove(&buffer->resource_destroy.link);
	wl_resource_add_destroy_listener(
			resource, &buffer->resource_destroy);
	buffer->resource_destroy.notify = buffer_resource_handle_destroy;
	buffer->resource = resource;
	return buffer;
}

bool wlr_buffer_get_dmabuf(struct wlr_buffer *buffer,
		struct wlr_dmabuf_attributes *attribs) {
	if (buffer->resource == NULL) {
		return false;
	}

	struct wl_resource *buffer_resource = buffer->resource;
	if (!wlr_dmabuf_v1_resource_is_buffer(buffer_resource)) {
		return false;
	}

	struct wlr_dmabuf_v1_buffer *dmabuf_buffer =
		wlr_dmabuf_v1_buffer_from_buffer_resource(buffer_resource);
	memcpy(attribs, &dmabuf_buffer->attributes,
		sizeof(struct wlr_dmabuf_attributes));
	return true;
}
