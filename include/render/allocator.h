#ifndef RENDER_ALLOCATOR
#define RENDER_ALLOCATOR

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_renderer;

struct wlr_allocator_interface {
	struct wlr_buffer *(*create_buffer)(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format);
	void (*destroy)(struct wlr_allocator *alloc);
};

struct wlr_allocator {
	const struct wlr_allocator_interface *impl;

	struct {
		struct wl_signal destroy;
	} events;
};

/**
 * Creates the adequate wlr_allocator given a backend and a renderer
 */
struct wlr_allocator *wlr_allocator_autocreate(struct wlr_backend *backend,
	struct wlr_renderer *renderer);
/**
 * Destroy the allocator.
 */
void wlr_allocator_destroy(struct wlr_allocator *alloc);
/**
 * Allocate a new buffer.
 *
 * When the caller is done with it, they must unreference it by calling
 * wlr_buffer_drop.
 */
struct wlr_buffer *wlr_allocator_create_buffer(struct wlr_allocator *alloc,
	int width, int height, const struct wlr_drm_format *format);

// For wlr_allocator implementors
void wlr_allocator_init(struct wlr_allocator *alloc,
	const struct wlr_allocator_interface *impl);

struct wlr_allocator *allocator_autocreate_with_drm_fd(
	struct wlr_backend *backend, struct wlr_renderer *renderer, int drm_fd);

#endif
