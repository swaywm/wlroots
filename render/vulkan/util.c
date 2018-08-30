#include <vulkan/vulkan.h>
#include <render/vulkan.h>

int wlr_vulkan_find_mem_type(struct wlr_vulkan *vulkan,
		VkMemoryPropertyFlags flags, uint32_t req_bits) {

	VkPhysicalDeviceMemoryProperties props;
	vkGetPhysicalDeviceMemoryProperties(vulkan->phdev, &props);

	for(unsigned i = 0u; i < props.memoryTypeCount; ++i) {
		if(req_bits & (1 << i)) {
			if((props.memoryTypes[i].propertyFlags & flags) == flags)
				return i;
		}
	}

	return -1;
}

const char *vulkan_strerror(VkResult err) {
	#define ERR_STR(r) case VK_ ##r: return #r
	switch (err) {
		ERR_STR(SUCCESS);
		ERR_STR(NOT_READY);
		ERR_STR(TIMEOUT);
		ERR_STR(EVENT_SET);
		ERR_STR(EVENT_RESET);
		ERR_STR(INCOMPLETE);
		ERR_STR(ERROR_OUT_OF_HOST_MEMORY);
		ERR_STR(ERROR_OUT_OF_DEVICE_MEMORY);
		ERR_STR(ERROR_INITIALIZATION_FAILED);
		ERR_STR(ERROR_DEVICE_LOST);
		ERR_STR(ERROR_MEMORY_MAP_FAILED);
		ERR_STR(ERROR_LAYER_NOT_PRESENT);
		ERR_STR(ERROR_EXTENSION_NOT_PRESENT);
		ERR_STR(ERROR_FEATURE_NOT_PRESENT);
		ERR_STR(ERROR_INCOMPATIBLE_DRIVER);
		ERR_STR(ERROR_TOO_MANY_OBJECTS);
		ERR_STR(ERROR_FORMAT_NOT_SUPPORTED);
		ERR_STR(ERROR_SURFACE_LOST_KHR);
		ERR_STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
		ERR_STR(SUBOPTIMAL_KHR);
		ERR_STR(ERROR_OUT_OF_DATE_KHR);
		ERR_STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
		ERR_STR(ERROR_VALIDATION_FAILED_EXT);
		default:
			return "<unknown>";
	}
	#undef STR
}

void vulkan_change_layout(VkCommandBuffer cb, VkImage img,
		VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
		VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta) {
	VkImageMemoryBarrier barrier = {0};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = ol;
	barrier.newLayout = nl;
	barrier.image = img;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.srcAccessMask = srca;
	barrier.dstAccessMask = dsta;

	vkCmdPipelineBarrier(cb, srcs, dsts, 0, 0, NULL, 0, NULL, 1, &barrier);
}
