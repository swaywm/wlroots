#include <vulkan/vulkan.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"

// reversed endianess of shm and vulkan
static const struct wlr_vk_pixel_format formats[] = {
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

const struct wlr_vk_pixel_format *vulkan_get_format_list(size_t *len) {
	*len = sizeof(formats) / sizeof(formats[0]);
	return formats;
}

const struct wlr_vk_pixel_format *vulkan_get_format_from_wl(
		enum wl_shm_format fmt) {
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		if (formats[i].wl_format == fmt) {
			return &formats[i];
		}
	}
	return NULL;
}

bool wlr_vk_query_format(struct wlr_vk_device *dev,
		struct wlr_vk_pixel_format_props *props, VkImageUsageFlags usage,
		VkImageTiling tiling) {

	VkResult res;
	struct wlr_vk_pixel_format *format = &props->format;

	VkPhysicalDeviceExternalImageFormatInfo eformat_info = {0};
	eformat_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
	eformat_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

	VkPhysicalDeviceImageFormatInfo2 format_info = {0};
	format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
	format_info.pNext = &eformat_info;
	format_info.tiling = tiling;
	format_info.usage = usage;
	format_info.type = VK_IMAGE_TYPE_2D;

	VkExternalImageFormatProperties eformat_props = {0};
	eformat_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

	VkImageFormatProperties2 format_props = {0};
	format_props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
	format_props.pNext = &eformat_props;
	// image format properties
	format_info.format = format->vk_format;
	format_info.pNext = &eformat_info;
	format_props.pNext = &eformat_props;

	res = vkGetPhysicalDeviceImageFormatProperties2(dev->phdev,
		&format_info, &format_props);
	if (res == VK_SUCCESS) {
		props->as_dmabuf = true;
		props->dma_features =
			eformat_props.externalMemoryProperties.externalMemoryFeatures;
	} else if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
		// combination not ok, try again without external handle
		format_info.pNext = NULL;
		format_props.pNext = NULL;
		res = vkGetPhysicalDeviceImageFormatProperties2(dev->phdev,
			&format_info, &format_props);
		if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
			return false;
		} else if (res != VK_SUCCESS) {
			wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2", res);
			return false;
		}

		props->as_dmabuf = false;
	} else if (res != VK_SUCCESS) {
		wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2", res);
		return false;
	}

	// add format as supported
	VkExtent3D me = format_props.imageFormatProperties.maxExtent;
	props->max_extent.width = me.width;
	props->max_extent.height = me.height;
	return true;
}
