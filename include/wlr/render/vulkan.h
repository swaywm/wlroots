#ifndef WLR_RENDER_VULKAN_H
#define WLR_RENDER_VULKAN_H

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_vk_instance;
struct wlr_vk_renderer;

// Creates all vulkan resources (inclusive instance) from scratch.
struct wlr_renderer *wlr_vk_renderer_create(struct wlr_backend *backend);
struct wlr_texture *wlr_vk_texture_from_pixels(struct wlr_vk_renderer *renderer,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	const void *data);

bool wlr_vk_present_queue_supported_xcb(struct wlr_vk_instance *instance,
	uintptr_t vk_physical_device, uint32_t qfam, void *xcb_connection_t,
	uint32_t visualid);
bool wlr_vk_present_queue_supported_wl(struct wlr_vk_instance *instance,
	uintptr_t vk_physical_device, uint32_t qfam, struct wl_display *remote);

#endif

