#include <assert.h>
#include <drm_fourcc.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"

#if defined(__linux__)
#include <sys/sysmacros.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#else
#error "Missing major/minor for this platform"
#endif

static const struct wlr_buffer_impl buffer_impl;

static struct wlr_vulkan_buffer *vulkan_buffer_from_buffer(
		struct wlr_buffer *wlr_buf) {
	assert(wlr_buf->impl == &buffer_impl);
	return (struct wlr_vulkan_buffer *)wlr_buf;
}

static bool buffer_get_dmabuf(struct wlr_buffer *wlr_buf,
		struct wlr_dmabuf_attributes *out) {
	struct wlr_vulkan_buffer *buf = vulkan_buffer_from_buffer(wlr_buf);
	memcpy(out, &buf->dmabuf, sizeof(buf->dmabuf));
	return true;
}

static void buffer_destroy(struct wlr_buffer *wlr_buf) {
	struct wlr_vulkan_buffer *buf = vulkan_buffer_from_buffer(wlr_buf);
	wlr_dmabuf_attributes_finish(&buf->dmabuf);
	vkFreeMemory(buf->alloc->device, buf->memory, NULL);
	vkDestroyImage(buf->alloc->device, buf->image, NULL);
	free(buf);
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.get_dmabuf = buffer_get_dmabuf,
};

static const struct wlr_allocator_interface allocator_impl;

static struct wlr_vulkan_allocator *vulkan_alloc_from_alloc(
		struct wlr_allocator *wlr_alloc) {
	assert(wlr_alloc->impl == &allocator_impl);
	return (struct wlr_vulkan_allocator *)wlr_alloc;
}

static struct wlr_buffer *allocator_create_buffer(
		struct wlr_allocator *wlr_alloc, int width, int height,
		const struct wlr_drm_format *drm_format) {
	struct wlr_vulkan_allocator *alloc = vulkan_alloc_from_alloc(wlr_alloc);

	// TODO: filter format/modifiers based on
	// VkPhysicalDeviceImageDrmFormatModifierInfoEXT

	VkFormat format = VK_FORMAT_B8G8R8A8_SRGB; // TODO

	// TODO: use VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	if (drm_format->len == 1 &&
			drm_format->modifiers[0] == DRM_FORMAT_MOD_LINEAR) {
		tiling = VK_IMAGE_TILING_LINEAR;
	}

	struct wlr_vulkan_buffer *buf = calloc(1, sizeof(*buf));
	if (buf == NULL) {
		return NULL;
	}
	wlr_buffer_init(&buf->base, &buffer_impl, width, height);
	buf->alloc = alloc;

	VkImageDrmFormatModifierListCreateInfoEXT drm_format_mod = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
		.drmFormatModifierCount = drm_format->len,
		.pDrmFormatModifiers = drm_format->modifiers,
	};
	VkExternalMemoryImageCreateInfo ext_mem = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.pNext = &drm_format_mod,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	VkImageCreateInfo img_create = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = &ext_mem,
		.imageType = VK_IMAGE_TYPE_2D,
		.extent = { .width = width, .height = height, .depth = 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.format = format,
		.tiling = tiling,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.samples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkResult res = vkCreateImage(alloc->device, &img_create, NULL, &buf->image);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkCreateImage failed");
		goto error_buf;
	}

	VkMemoryRequirements mem_reqs = {0};
	vkGetImageMemoryRequirements(alloc->device, buf->image, &mem_reqs);

	VkExportMemoryAllocateInfo export_mem = {
		.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};
	VkMemoryAllocateInfo mem_alloc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = &export_mem,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = 0, // TODO
	};

	res = vkAllocateMemory(alloc->device, &mem_alloc, NULL, &buf->memory);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkAllocateMemory failed");
		goto error_image;
	}

	res = vkBindImageMemory(alloc->device, buf->image, buf->memory, 0);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkBindImageMemory failed");
		goto error_memory;
	}

	VkMemoryGetFdInfoKHR mem_get_fd = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
		.memory = buf->memory,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};

	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)
		vkGetInstanceProcAddr(alloc->instance, "vkGetMemoryFdKHR");
	assert(vkGetMemoryFdKHR != NULL);

	int fd;
	res = vkGetMemoryFdKHR(alloc->device, &mem_get_fd, &fd);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkGetMemoryFdKHR failed");
		goto error_memory;
	}

	VkImageSubresource img_subres = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.arrayLayer = 0,
	};
	VkSubresourceLayout subres_layout = {0};
	vkGetImageSubresourceLayout(alloc->device, buf->image, &img_subres,
		&subres_layout);

	buf->dmabuf = (struct wlr_dmabuf_attributes){
		.format = DRM_FORMAT_ARGB8888, // TODO
		.modifier = DRM_FORMAT_MOD_INVALID, // TODO
		.width = width,
		.height = height,
		.n_planes = 1, // TODO
		.fd = { fd },
		.stride = { subres_layout.rowPitch },
	};

	return &buf->base;

error_memory:
	vkFreeMemory(buf->alloc->device, buf->memory, NULL);
error_image:
	vkDestroyImage(buf->alloc->device, buf->image, NULL);
error_buf:
	free(buf);
	return NULL;
}

static void allocator_destroy(struct wlr_allocator *wlr_alloc) {
	struct wlr_vulkan_allocator *alloc = vulkan_alloc_from_alloc(wlr_alloc);
	vkDestroyDevice(alloc->device, NULL);
	vkDestroyInstance(alloc->instance, NULL);
	free(alloc);
}

static const struct wlr_allocator_interface allocator_impl = {
	.create_buffer = allocator_create_buffer,
	.destroy = allocator_destroy,
};

static VkPhysicalDevice find_phy_device_from_drm_fd(VkInstance instance,
		int drm_fd) {
	struct stat drm_stat;
	if (fstat(drm_fd, &drm_stat) != 0) {
		wlr_log_errno(WLR_ERROR, "fstat failed");
		return VK_NULL_HANDLE;
	}

	int64_t maj = (int64_t)major(drm_stat.st_rdev);
	int64_t min = (int64_t)minor(drm_stat.st_rdev);

	uint32_t devices_len = 0;
	vkEnumeratePhysicalDevices(instance, &devices_len, NULL);
	if (devices_len == 0) {
		wlr_log(WLR_ERROR, "No physical device found");
		return VK_NULL_HANDLE;
	}

	VkPhysicalDevice *devices = calloc(devices_len, sizeof(VkPhysicalDevice));
	if (devices == NULL) {
		return VK_NULL_HANDLE;
	}

	VkResult res = vkEnumeratePhysicalDevices(instance, &devices_len, devices);
	if (res != VK_SUCCESS) {
		free(devices);
		wlr_log(WLR_ERROR, "vkEnumeratePhysicalDevices failed");
		return VK_NULL_HANDLE;
	}

	VkPhysicalDevice dev = VK_NULL_HANDLE;
	for (size_t i = 0; i < devices_len; i++) {
		VkPhysicalDeviceDrmPropertiesEXT drm_props = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
		};
		VkPhysicalDeviceProperties2 props = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &drm_props,
		};
		vkGetPhysicalDeviceProperties2(devices[i], &props);

		if (drm_props.hasPrimary && drm_props.primaryMajor == maj &&
				drm_props.primaryMinor == min) {
			dev = devices[i];
		}
		if (drm_props.hasRender && drm_props.renderMajor == maj &&
				drm_props.renderMinor == min) {
			dev = devices[i];
		}

		if (dev != VK_NULL_HANDLE) {
			wlr_log(WLR_DEBUG, "Physical device: %s",
				props.properties.deviceName);
			break;
		}
	}

	free(devices);

	return dev;
}

static bool create_device(struct wlr_vulkan_allocator *alloc) {
	const char *exts[] = {
		VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
	};
	VkDeviceCreateInfo device_create = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.enabledExtensionCount = sizeof(exts) / sizeof(exts[0]),
		.ppEnabledExtensionNames = exts,
	};
	VkResult res =
		vkCreateDevice(alloc->phy_device, &device_create, NULL, &alloc->device);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkCreateDevice failed");
		return false;
	}
	return true;
}

struct wlr_vulkan_allocator *wlr_vulkan_allocator_create(int drm_fd) {
	struct wlr_vulkan_allocator *alloc = calloc(1, sizeof(*alloc));
	if (alloc == NULL) {
		return NULL;
	}
	wlr_allocator_init(&alloc->base, &allocator_impl);

	VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "wlroots",
		.apiVersion = VK_API_VERSION_1_1,
	};
	VkInstanceCreateInfo instance_create = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
	};
	VkResult res = vkCreateInstance(&instance_create, NULL, &alloc->instance);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "vkCreateInstance failed");
		return NULL;
	}

	alloc->phy_device = find_phy_device_from_drm_fd(alloc->instance, drm_fd);
	if (alloc->phy_device == VK_NULL_HANDLE) {
		wlr_log(WLR_ERROR, "Failed to find physical device from DRM FD");
		return NULL;
	}

	if (!create_device(alloc)) {
		return NULL;
	}

	return alloc;
}
