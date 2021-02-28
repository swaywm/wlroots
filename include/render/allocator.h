#ifndef RENDER_ALLOCATOR
#define RENDER_ALLOCATOR

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct wlr_allocator;

struct wlr_allocator_interface {
	struct wlr_buffer *(*create_buffer)(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format,
		void *data);
	void (*destroy)(struct wlr_allocator *alloc);
};

struct wlr_allocator {
	const struct wlr_allocator_interface *impl;

	struct {
		struct wl_signal destroy;
	} events;
};

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
	int width, int height, const struct wlr_drm_format *format, void *data);

// For wlr_allocator implementors
void wlr_allocator_init(struct wlr_allocator *alloc,
	const struct wlr_allocator_interface *impl);

#endif
