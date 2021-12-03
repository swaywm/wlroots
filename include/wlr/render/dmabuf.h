/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_DMABUF_H
#define WLR_RENDER_DMABUF_H

#include <stdbool.h>
#include <stdint.h>

#define WLR_DMABUF_MAX_PLANES 4

struct wlr_dmabuf_attributes {
	int32_t width, height;
	uint32_t format;
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
/**
 * Clones the DMA-BUF attributes.
 */
bool wlr_dmabuf_attributes_copy(struct wlr_dmabuf_attributes *dst,
	const struct wlr_dmabuf_attributes *src);

#endif
