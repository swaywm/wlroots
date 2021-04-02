#ifndef RENDER_ALLOCATOR
#define RENDER_ALLOCATOR

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/config.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_format_set.h>

struct wlr_allocator;
struct wlr_drm_buffer;

struct wlr_allocator_interface {
	struct wlr_buffer *(*create_buffer)(struct wlr_allocator *alloc,
		int width, int height, const struct wlr_drm_format *format);
	void (*destroy)(struct wlr_allocator *alloc);

	/**
	 * methods for DMABuf importing, zero-copy, only a drm backend
	 * would call it. The importing buffer would be a wlr_client_buffer
	 * \param buf
	 * actually. You need to check whether the buffer is from the same
	 * interface.
	 */
	struct wlr_drm_buffer *(*import_buffer)(struct wlr_allocator *alloc,
		struct wlr_buffer *buf);
};

struct wlr_allocator {
	const struct wlr_allocator_interface *impl;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_drm_buffer {
	struct wlr_buffer base;
	struct wl_list link;

	/* Only a importing buffer should use this field */
	struct wlr_buffer *orig_buf;

	/* drm device fd */
	int drm_fd;
	/* drm fb id */
	uint32_t fb_id;
	/**
	 * a imported buffer doesn't need to use this property
	 */
	struct wlr_dmabuf_attributes dmabuf;

	/* GBM data or DRM dumb data */
	void *impl_data;
	void (*destroy_impl_data)(void *);
};

/**
 * Return an allocator could create drm buffers, which allocator would be
 * used depends on the built-time configure.
 */
struct wlr_allocator *wlr_allocator_create_with_drm_fd(int fd);

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

/**
 * Import a wayland buffer and build a DRM framebuffer for it, it would
 * hold the a reference to the orignal buffer until the new created buffer
 * is destroyed.
 */
struct wlr_drm_buffer *wlr_allocator_import(struct wlr_allocator *alloc,
	struct wlr_buffer *buf);

// For wlr_allocator implementors
void wlr_allocator_init(struct wlr_allocator *alloc,
	const struct wlr_allocator_interface *impl);

/**
 * Check whether it is a instance of wl_drm_buffer
 */
struct wlr_drm_buffer *wlr_drm_buffer_cast(struct wlr_buffer *wlr_buffer);

struct wlr_drm_buffer *wlr_drm_buffer_create(void *data,
					     void (*destroy_impl_data)(void *));

#endif
