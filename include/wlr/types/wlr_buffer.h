#ifndef WLR_TYPES_WLR_BUFFER_H
#define WLR_TYPES_WLR_BUFFER_H

#include <pixman.h>
#include <wayland-server.h>

struct wlr_buffer {
	struct wl_resource *resource;
	struct wlr_texture *texture;
	bool released;
	size_t n_refs;

	struct wl_listener resource_destroy;
};

struct wlr_renderer;

// Checks if a resource is a wl_buffer.
bool wlr_resource_is_buffer(struct wl_resource *resource);
// Returns the buffer size.
bool wlr_buffer_get_resource_size(struct wl_resource *resource,
	struct wlr_renderer *renderer, int *width, int *height);

// Uploads the texture to the GPU and references it.
struct wlr_buffer *wlr_buffer_create(struct wlr_renderer *renderer,
	struct wl_resource *resource);
// References and unreferences the buffer.
void wlr_buffer_ref(struct wlr_buffer *buffer);
void wlr_buffer_unref(struct wlr_buffer *buffer);
// Tries to update the texture in the provided buffer. This destroys `buffer`
// and returns a new buffer.
// Fails if `buffer->n_refs` > 1 or if the texture isn't mutable.
struct wlr_buffer *wlr_buffer_apply_damage(struct wlr_buffer *buffer,
	struct wl_resource *resource, pixman_region32_t *damage);

#endif
