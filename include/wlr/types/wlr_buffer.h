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
#include <wayland-server.h>

/**
 * A client buffer.
 * The buffers contents are reference counted.
 */
struct wlr_buffer {
	/**
	 * The buffer resource, if any. Will be NULL if the client destroys it.
	 */
	struct wl_resource *resource;
	/**
	 * The buffer's texture, if any. A buffer will not have a texture if the
	 * client destroys the buffer before it has been released.
	 */
	struct wlr_texture *texture;
	bool released; // released, i.e. might be modified by client atm
	bool destroyed; // destroyed by client, can be destroyed if no longer needed
	bool keep; // whether to keep this buffer even though no one uses it
	size_t n_refs; // content ref count: (n_refs == 0) => released [invariant]

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
bool wlr_buffer_get_resource_size(struct wl_resource *resource,
	struct wlr_renderer *renderer, int *width, int *height);

/**
 * Get a wlr_buffer object associated with the wl_buffer `resource`.
 * When there already is one associated it will be reused, making sure
 * that the textures contents match the buffer resource.
 * Otherwise creates a new wlr_buffer which texture is usable with `renderer`.
 * May returns NULL on failure.
 */
struct wlr_buffer *wlr_buffer_get(struct wlr_renderer *renderer,
	struct wl_resource *resource);
/**
 * Reference the buffer.
 * As long as the reference is valid (`wlr_buffer_unref` nor
 * `wlr_buffer_apply_damage` called) the wlr_buffer object is guaranteed to
 * stay alive and its contents the same.
 */
struct wlr_buffer *wlr_buffer_ref(struct wlr_buffer *buffer);
/**
 * Unreference the buffer. After this call, `buffer` may not be accessed
 * anymore.
 */
void wlr_buffer_unref(struct wlr_buffer *buffer);
/**
 * Try to update the buffer's content. On success, returns the updated buffer
 * and destroys the provided `buffer`. On error, `buffer` is intact and NULL is
 * returned.
 *
 * Fails if there's more than one reference to the buffer or if the texture
 * isn't mutable.
 */
struct wlr_buffer *wlr_buffer_apply_damage(struct wlr_buffer *buffer,
	struct wl_resource *resource, pixman_region32_t *damage);

#endif
