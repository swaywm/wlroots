#ifndef RENDER_GBM_ALLOCATOR_H
#define RENDER_GBM_ALLOCATOR_H

#include <gbm.h>
#include <wlr/types/wlr_buffer.h>
#include "render/allocator.h"

struct wlr_gbm_buffer {
	struct wlr_buffer base;

	struct gbm_bo *gbm_bo;
	struct wlr_dmabuf_attributes dmabuf;
};

struct wlr_gbm_allocator {
	struct wlr_allocator base;

	int fd;
	struct gbm_device *gbm_device;
};

/**
 * Creates a new GBM allocator from a DRM FD.
 *
 * Takes ownership over the FD.
 */
struct wlr_gbm_allocator *wlr_gbm_allocator_create(int drm_fd);

#endif
