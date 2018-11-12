#ifndef WLR_RENDER_SWAPCHAIN_H
#define WLR_RENDER_SWAPCHAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <gbm.h>
#include <wayland-server.h>

#include <wlr/render/allocator.h>

struct wlr_backend;

enum wlr_swapchain_flags {
	WLR_SWAPCHAIN_TRIPLE_BUFFERED = 1,
};

struct wlr_swapchain_image {
	struct wlr_image *img;
	uint64_t seq;
	bool aquired;

	struct wl_listener release;
};

struct wlr_swapchain {
	struct wlr_allocator *alloc;
	uint32_t flags;

	size_t num_images;
	struct wlr_swapchain_image images[3];

	uint64_t seq;
};

struct wlr_swapchain *wlr_swapchain_create(struct wlr_allocator *alloc,
		struct wlr_backend *backend, uint32_t width, uint32_t height,
		uint32_t format, size_t num_modifiers,
		const uint64_t *modifiers, uint32_t flags);

void wlr_swapchain_destroy(struct wlr_swapchain *sc);

struct wlr_image *wlr_swapchain_aquire(struct wlr_swapchain *sc);

#endif
