#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <vulkan/vulkan.h>
#include <wlr/types/wlr_buffer.h>
#include "render/allocator.h"

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT 1000353000

#define VK_EXT_physical_device_drm 1
#define VK_EXT_PHYSICAL_DEVICE_DRM_SPEC_VERSION 1
#define VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME "VK_EXT_physical_device_drm"
typedef struct VkPhysicalDeviceDrmPropertiesEXT {
    VkStructureType    sType;
    void*              pNext;
    VkBool32           hasPrimary;
    VkBool32           hasRender;
    int64_t            primaryMajor;
    int64_t            primaryMinor;
    int64_t            renderMajor;
    int64_t            renderMinor;
} VkPhysicalDeviceDrmPropertiesEXT;

struct wlr_vulkan_buffer {
	struct wlr_buffer base;
	struct wlr_vulkan_allocator *alloc;

	VkImage image;
	VkDeviceMemory memory;

	struct wlr_dmabuf_attributes dmabuf;
};

struct wlr_vulkan_allocator {
	struct wlr_allocator base;

	VkInstance instance;
	VkPhysicalDevice phy_device;

	VkDevice device;
};

struct wlr_vulkan_allocator *wlr_vulkan_allocator_create(int drm_fd);

#endif
