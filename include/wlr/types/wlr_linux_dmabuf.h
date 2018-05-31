#ifndef WLR_TYPES_WLR_LINUX_DMABUF_H
#define WLR_TYPES_WLR_LINUX_DMABUF_H

#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/render/dmabuf.h>

struct wlr_dmabuf_buffer {
	struct wlr_renderer *renderer;
	struct wl_resource *buffer_resource;
	struct wl_resource *params_resource;
	struct wlr_dmabuf_attributes attributes;
	bool has_modifier;
};

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
