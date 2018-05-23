#ifndef WLR_TYPES_WLR_LINUX_DMABUF_H
#define WLR_TYPES_WLR_LINUX_DMABUF_H

#define WLR_LINUX_DMABUF_MAX_PLANES 4

#include <stdint.h>
#include <wayland-server-protocol.h>

/* So we don't have to pull in linux specific drm headers */
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID ((1ULL<<56) - 1)
#endif

enum {
	WLR_DMABUF_BUFFER_ATTRIBS_FLAGS_Y_INVERT = 1,
	WLR_DMABUF_BUFFER_ATTRIBS_FLAGS_INTERLACED = 2,
	WLR_DMABUF_BUFFER_ATTRIBS_FLAGS_BOTTOM_FIRST = 4,
};

struct wlr_dmabuf_buffer_attribs {
	/* set via params_add */
	int n_planes;
	uint32_t offset[WLR_LINUX_DMABUF_MAX_PLANES];
	uint32_t stride[WLR_LINUX_DMABUF_MAX_PLANES];
	uint64_t modifier[WLR_LINUX_DMABUF_MAX_PLANES];
	int fd[WLR_LINUX_DMABUF_MAX_PLANES];
	/* set via params_create */
	int32_t width, height;
	uint32_t format;
	uint32_t flags;
};

struct wlr_dmabuf_buffer {
	struct wlr_renderer *renderer;
	struct wl_resource *buffer_resource;
	struct wl_resource *params_resource;
	struct wlr_dmabuf_buffer_attribs attributes;
};

/**
 * Closes all file descriptors in the DMA-BUF attributes.
 */
void wlr_dmabuf_buffer_attribs_finish(
	struct wlr_dmabuf_buffer_attribs *attribs);

/**
 * Returns true if the given resource was created via the linux-dmabuf
 * buffer protocol, false otherwise
 */
bool wlr_dmabuf_resource_is_buffer(struct wl_resource *buffer_resource);

/**
 * Returns the wlr_dmabuf_buffer if the given resource was created
 * via the linux-dmabuf buffer protocol
 */
struct wlr_dmabuf_buffer *wlr_dmabuf_buffer_from_buffer_resource(
	struct wl_resource *buffer_resource);

/**
 * Returns the wlr_dmabuf_buffer if the given resource was created
 * via the linux-dmabuf params protocol
 */
struct wlr_dmabuf_buffer *wlr_dmabuf_buffer_from_params_resource(
	struct wl_resource *params_resource);

/* the protocol interface */
struct wlr_linux_dmabuf {
	struct wl_global *wl_global;
	struct wlr_renderer *renderer;
	struct wl_list wl_resources;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
	struct wl_listener renderer_destroy;
};

/**
 * Create linux-dmabuf interface
 */
struct wlr_linux_dmabuf *wlr_linux_dmabuf_create(struct wl_display *display,
	struct wlr_renderer *renderer);
/**
 * Destroy the linux-dmabuf interface
 */
void wlr_linux_dmabuf_destroy(struct wlr_linux_dmabuf *linux_dmabuf);

/**
 * Returns the wlr_linux_dmabuf if the given resource was created
 * via the linux_dmabuf protocol
 */
struct wlr_linux_dmabuf *wlr_linux_dmabuf_from_resource(
	struct wl_resource *resource);

#endif
