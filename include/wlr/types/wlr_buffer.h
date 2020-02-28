/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_BUFFER_H
#define WLR_TYPES_WLR_BUFFER_H

#include <pixman.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>

struct wlr_buffer;

struct wlr_buffer_impl {
	void (*destroy)(struct wlr_buffer *buffer);
	bool (*get_dmabuf)(struct wlr_buffer *buffer,
		struct wlr_dmabuf_attributes *attribs);
};

struct wlr_buffer {
	const struct wlr_buffer_impl *impl;

	size_t n_refs;
};

void wlr_buffer_init(struct wlr_buffer *buffer,
	const struct wlr_buffer_impl *impl);
/**
 * Reference the buffer.
 */
struct wlr_buffer *wlr_buffer_ref(struct wlr_buffer *buffer);
/**
 * Unreference the buffer. After this call, `buffer` may not be accessed
 * anymore.
 */
void wlr_buffer_unref(struct wlr_buffer *buffer);
/**
 * Reads the DMA-BUF attributes of the buffer. If this buffer isn't a DMA-BUF,
 * returns false.
 */
bool wlr_buffer_get_dmabuf(struct wlr_buffer *buffer,
	struct wlr_dmabuf_attributes *attribs);

/**
 * A client buffer.
 */
struct wlr_client_buffer {
	struct wlr_buffer base;

	/**
	 * The buffer resource, if any. Will be NULL if the client destroys it.
	 */
	struct wl_resource *resource;
	/**
	 * Whether a release event has been sent to the resource.
	 */
	bool resource_released;
	/**
	 * The buffer's texture, if any. A buffer will not have a texture if the
	 * client destroys the buffer before it has been released.
	 */
	struct wlr_texture *texture;
	bool released;
	size_t n_refs;

	struct wl_listener resource_destroy;
};

struct wlr_renderer;

/**
 * Check if a resource is a wl_buffer resource.
 */
bool wlr_resource_is_buffer(struct wl_resource *resource);
/**
 * Get the size of a wl_buffer resource.
 */
bool wlr_resource_get_buffer_size(struct wl_resource *resource,
	struct wlr_renderer *renderer, int *width, int *height);
/**
 * Upload a buffer to the GPU and reference it.
 */
struct wlr_client_buffer *wlr_client_buffer_create(
	struct wlr_renderer *renderer, struct wl_resource *resource);
/**
 * Try to update the buffer's content. On success, returns the updated buffer
 * and destroys the provided `buffer`. On error, `buffer` is intact and NULL is
 * returned.
 *
 * Fails if there's more than one reference to the buffer or if the texture
 * isn't mutable.
 */
struct wlr_client_buffer *wlr_client_buffer_apply_damage(
	struct wlr_client_buffer *buffer, struct wl_resource *resource,
	pixman_region32_t *damage);

#endif
