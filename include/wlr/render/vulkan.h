#ifndef WLR_RENDER_VULKAN_H
#define WLR_RENDER_VULKAN_H

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wlr_vulkan;
struct wlr_vk_renderer;

struct wlr_renderer *wlr_vk_renderer_create(struct wlr_backend *backend);
struct wlr_texture *wlr_vk_texture_from_pixels(struct wlr_vk_renderer *renderer,
	enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
	const void *data);

// struct wlr_texture *wlr_vulkan_texture_from_wl_drm(struct wlr_vulkan *vulkan,
// 	struct wl_resource *data);
// struct wlr_texture *wlr_vulkan_texture_from_dmabuf(struct wlr_vulkan *vulkan,
// 	struct wlr_dmabuf_attributes *attribs);

#endif

