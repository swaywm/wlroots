#include <vulkan/vulkan.h>
#include "render/vulkan.h"

// reversed endianess, as in opengl
static const struct wlr_vulkan_pixel_format formats[] = {
	{
		.wl_format = WL_SHM_FORMAT_ARGB8888,
		.bpp = 32,
		.vk_format = VK_FORMAT_B8G8R8A8_UNORM,
		.has_alpha = true,
	},
	{
		.wl_format = WL_SHM_FORMAT_XRGB8888,
		.bpp = 32,
		.vk_format = VK_FORMAT_B8G8R8A8_UNORM,
		.has_alpha = false,
	},
	{
		.wl_format = WL_SHM_FORMAT_XBGR8888,
		.bpp = 32,
		.vk_format = VK_FORMAT_R8G8B8A8_UNORM,
		.has_alpha = false,
	},
	{
		.wl_format = WL_SHM_FORMAT_ABGR8888,
		.bpp = 32,
		.vk_format = VK_FORMAT_R8G8B8A8_UNORM,
		.has_alpha = true,
	},
};

static const enum wl_shm_format wl_formats[] = {
	WL_SHM_FORMAT_ARGB8888,
	WL_SHM_FORMAT_XRGB8888,
	WL_SHM_FORMAT_ABGR8888,
	WL_SHM_FORMAT_XBGR8888,
};

const struct wlr_vulkan_pixel_format *get_vulkan_format_from_wl(
		enum wl_shm_format fmt) {
	for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
		if (formats[i].wl_format == fmt) {
			return &formats[i];
		}
	}
	return NULL;
}

const enum wl_shm_format *get_vulkan_formats(size_t *len) {
	*len = sizeof(wl_formats) / sizeof(wl_formats[0]);
	return wl_formats;
}
