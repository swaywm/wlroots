/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_DMABUF_H
#define WLR_RENDER_DMABUF_H

#include <stdint.h>

// So we don't have to pull in linux specific drm headers
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL<<56) - 1)
#endif

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

#define WLR_DMABUF_MAX_PLANES 4

enum wlr_dmabuf_attributes_flags {
	WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT = 1,
	WLR_DMABUF_ATTRIBUTES_FLAGS_INTERLACED = 2,
	WLR_DMABUF_ATTRIBUTES_FLAGS_BOTTOM_FIRST = 4,
};

struct wlr_dmabuf_attributes {
	int32_t width, height;
	uint32_t format;
	uint32_t flags; // enum wlr_dmabuf_attributes_flags
	uint64_t modifier;

	int n_planes;
	uint32_t offset[WLR_DMABUF_MAX_PLANES];
	uint32_t stride[WLR_DMABUF_MAX_PLANES];
	int fd[WLR_DMABUF_MAX_PLANES];
};

/**
 * Closes all file descriptors in the DMA-BUF attributes.
 */
void wlr_dmabuf_attributes_finish(struct wlr_dmabuf_attributes *attribs);

#endif
