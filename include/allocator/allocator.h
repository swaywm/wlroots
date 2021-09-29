#ifndef RENDER_ALLOCATOR_ALLOCATOR_H
#define RENDER_ALLOCATOR_ALLOCATOR_H

#include <wlr/allocator/wlr_allocator.h>

struct wlr_allocator;
struct wlr_backend;
struct wlr_drm_format;
struct wlr_renderer;

/**
 * Allocate a new buffer.
 *
 * When the caller is done with it, they must unreference it by calling
 * wlr_buffer_drop.
 */
struct wlr_buffer *wlr_allocator_create_buffer(struct wlr_allocator *alloc,
	int width, int height, const struct wlr_drm_format *format);

struct wlr_allocator *allocator_autocreate_with_drm_fd(
	struct wlr_backend *backend, struct wlr_renderer *renderer, int drm_fd);

#endif
