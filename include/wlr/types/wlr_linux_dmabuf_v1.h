/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_LINUX_DMABUF_H
#define WLR_TYPES_WLR_LINUX_DMABUF_H

#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/render/dmabuf.h>

struct wlr_dmabuf_v1_buffer {
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
bool wlr_dmabuf_v1_resource_is_buffer(struct wl_resource *buffer_resource);

/**
 * Returns the wlr_dmabuf_buffer if the given resource was created
 * via the linux-dmabuf buffer protocol
 */
struct wlr_dmabuf_v1_buffer *wlr_dmabuf_v1_buffer_from_buffer_resource(
	struct wl_resource *buffer_resource);

/**
 * Returns the wlr_dmabuf_buffer if the given resource was created
 * via the linux-dmabuf params protocol
 */
struct wlr_dmabuf_v1_buffer *wlr_dmabuf_v1_buffer_from_params_resource(
	struct wl_resource *params_resource);

/* the protocol interface */
struct wlr_linux_dmabuf_v1 {
	struct wl_global *global;
	struct wlr_renderer *renderer;
	struct wl_list resources;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy;
	struct wl_listener renderer_destroy;
};

/**
 * Create linux-dmabuf interface
 */
struct wlr_linux_dmabuf_v1 *wlr_linux_dmabuf_v1_create(struct wl_display *display,
	struct wlr_renderer *renderer);
/**
 * Destroy the linux-dmabuf interface
 */
void wlr_linux_dmabuf_v1_destroy(struct wlr_linux_dmabuf_v1 *linux_dmabuf);

/**
 * Returns the wlr_linux_dmabuf if the given resource was created
 * via the linux_dmabuf protocol
 */
struct wlr_linux_dmabuf_v1 *wlr_linux_dmabuf_v1_from_resource(
	struct wl_resource *resource);

#endif
