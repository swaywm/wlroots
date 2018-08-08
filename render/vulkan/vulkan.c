#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <render/vulkan.h>
#include <wlr/util/log.h>
#include <wlr/version.h>

// #define WLR_VK_PROC_DEV(dev, name) PFN_vk##name fp##name =
// 	(PFN_vk##name) vkGetDeviceProcAddr(dev, "vk" #name);
//
// #define WLR_VK_PROC_INI(ini, name) PFN_vk##name fp##name =
// 	(PFN_vk##name) vkGetInstanceProcAddr(ini, "vk" #name);

#define wlr_vulkan_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

void wlr_vulkan_destroy(struct wlr_vulkan *vulkan) {
	if (vulkan->dev) {
		vkDestroyDevice(vulkan->dev, NULL);
	}

	if (vulkan->messenger && vulkan->api.destroyDebugUtilsMessengerEXT) {
		vulkan->api.destroyDebugUtilsMessengerEXT(vulkan->instance,
			vulkan->messenger, NULL);
	}

	if (vulkan->instance) {
		vkDestroyInstance(vulkan->instance, NULL);
	}

	free(vulkan);
}

static int find_queue_family(const VkQueueFamilyProperties* props,
		uint32_t prop_count, VkQueueFlags flags) {
	for(unsigned i = 0; i < prop_count; ++i) {
		if((props[i].queueFlags & flags) == flags) {
			return i;
		}
	}

	return -1;
}

static VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* debug_data,
		void* data) {

	enum wlr_log_importance importance;
	switch(severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			importance = WLR_ERROR;
			break;
		default:
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			importance = WLR_INFO;
			break;
	}

	wlr_log(importance, "%s", debug_data->pMessage);
	if (debug_data->queueLabelCount > 0) {
		const char* name = debug_data->pQueueLabels[0].pLabelName;
		if (name) {
			wlr_log(importance, "    last label '%s'", name);
		}
	}

	for(unsigned i = 0; i < debug_data->objectCount; ++i) {
		if (debug_data->pObjects[i].pObjectName) {
			wlr_log(importance, "    involving '%s'", debug_data->pMessage);
		}
	}

	return false;
}

static bool init_instance(struct wlr_vulkan *vulkan, unsigned int ext_count,
		const char **exts, bool debug) {
	uint32_t ecount = 0;
	VkResult res = vkEnumerateInstanceExtensionProperties(NULL, &ecount, NULL);
	if ((res != VK_SUCCESS) || (ecount == 0)) {
		wlr_vulkan_error("Could not enumerate instance extensions (1)", res);
		return false;
	}

	VkExtensionProperties *eprops = calloc(ecount, sizeof(VkExtensionProperties));
	if (!eprops) {
		wlr_log(WLR_ERROR, "allocation failed");
		return false;
	}

	res = vkEnumerateInstanceExtensionProperties(NULL, &ecount, eprops);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Could not enumerate instance extensions (2)", res);
		free(eprops);
		return false;
	}

	int ext_off = 1 + debug;
	const char *extensions[ext_count + ext_off];
	memcpy(extensions + ext_off, exts, ext_count * sizeof(*exts));
	extensions[ext_off - 1] = VK_KHR_SURFACE_EXTENSION_NAME;
	if (debug) {
		extensions[0] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	}

	const char* const* ext_start = extensions;
	bool debug_utils_found = true;

	for(size_t i = 0; i < ext_count + 1; ++i) {
		bool found = false;
		for(size_t j = 0; j < ext_count; ++j) {
			if (strcmp(eprops[j].extensionName, extensions[i]) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			wlr_log(WLR_ERROR, "Could not find extension %s", extensions[i]);
			if (debug && i == 0) { // debug utils, not critical
				debug_utils_found = false;
				++ext_start;
				--ext_off;
				continue;
			}

			free(eprops);
			return false;
		}
	}

	free(eprops);

	// TODO: use compositor version and name provided somewhere?
	VkApplicationInfo application_info = {0};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "wlroots-compositor";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "wlroots";
	application_info.engineVersion = WLR_VERSION_NUM;
	application_info.apiVersion = VK_MAKE_VERSION(1,0,0);

	const char *layer_name = "VK_LAYER_LUNARG_standard_validation";

	VkInstanceCreateInfo instance_info = {0};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application_info;
	instance_info.enabledExtensionCount = ext_count + ext_off;
	instance_info.ppEnabledExtensionNames = ext_start;
	instance_info.enabledLayerCount = debug;
	instance_info.ppEnabledLayerNames = &layer_name;

	res = vkCreateInstance(&instance_info, NULL, &vulkan->instance);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Could not create instance", res);
		return false;
	}

	if (debug_utils_found) {
		vulkan->api.createDebugUtilsMessengerEXT =
			(PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					vulkan->instance, "vkCreateDebugUtilsMessengerEXT");
		vulkan->api.destroyDebugUtilsMessengerEXT =
			(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					vulkan->instance, "vkDestroyDebugUtilsMessengerEXT");

		if(vulkan->api.createDebugUtilsMessengerEXT) {
			VkDebugUtilsMessageSeverityFlagsEXT severity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			VkDebugUtilsMessageTypeFlagsEXT types =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

			VkDebugUtilsMessengerCreateInfoEXT debug_info = {0};
			debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debug_info.messageSeverity = severity;
			debug_info.messageType = types;
			debug_info.pfnUserCallback = &debug_callback;
			debug_info.pUserData = &vulkan;

			vulkan->api.createDebugUtilsMessengerEXT(vulkan->instance,
				&debug_info, NULL, &vulkan->messenger);
		} else {
			wlr_log(WLR_ERROR, "vkCreateDebugUtilsMessengerEXT not found");
		}
	}

	return true;
}

static bool init_device(struct wlr_vulkan *vulkan, unsigned int ext_count,
		const char **exts) {
	// TODO: don't just choose the first device
	uint32_t num_devs = 1;
	VkResult ret = vkEnumeratePhysicalDevices(vulkan->instance, &num_devs,
		&vulkan->phdev);
	if (ret != VK_SUCCESS || !vulkan->phdev) {
		wlr_log(WLR_ERROR, "Could not retrieve physical device");
		return false;
	}

	uint32_t qfam_count;
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan->phdev, &qfam_count, NULL);
	VkQueueFamilyProperties *queue_props = calloc(qfam_count,
		sizeof(queue_props));
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan->phdev, &qfam_count,
		queue_props);

	// TODO: choose present queue family based on created surface(s)?
	vulkan->graphics_queue_fam = find_queue_family(queue_props, qfam_count,
		VK_QUEUE_GRAPHICS_BIT);
	vulkan->present_queue_fam = vulkan->graphics_queue_fam;

	bool one_queue = (vulkan->present_queue_fam == vulkan->graphics_queue_fam);

	float prio = 1.f;
	VkDeviceQueueCreateInfo graphics_queue_info = {0};
	graphics_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	graphics_queue_info.queueFamilyIndex = vulkan->graphics_queue_fam;
	graphics_queue_info.queueCount = 1;
	graphics_queue_info.pQueuePriorities = &prio;

	VkDeviceQueueCreateInfo present_queue_info = graphics_queue_info;
	present_queue_info.queueFamilyIndex = vulkan->present_queue_fam;

	VkDeviceQueueCreateInfo queue_infos[2] = {
		graphics_queue_info,
		present_queue_info
	};

	const char *extensions[ext_count + 3];
	extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	extensions[1] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
	extensions[2] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
	memcpy(extensions + 3, exts, ext_count * sizeof(*exts));

	VkDeviceCreateInfo dev_info = {0};
	dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dev_info.queueCreateInfoCount = one_queue ? 1 : 2;
	dev_info.pQueueCreateInfos = queue_infos;
	dev_info.enabledExtensionCount = ext_count + 3;
	dev_info.ppEnabledExtensionNames = extensions;

	ret = vkCreateDevice(vulkan->phdev, &dev_info, NULL, &vulkan->dev);
	if (ret != VK_SUCCESS){
		wlr_log(WLR_ERROR, "Failed to create vulkan device");
		return false;
	}

	vkGetDeviceQueue(vulkan->dev, vulkan->graphics_queue_fam, 0,
		&vulkan->graphics_queue);
	if (one_queue) {
		vulkan->present_queue = vulkan->graphics_queue;
	} else {
		vkGetDeviceQueue(vulkan->dev, vulkan->present_queue_fam, 0,
			&vulkan->present_queue);
	}

	return true;
}

static void destroy_swapchain_buffers(struct wlr_vk_swapchain *swapchain) {
	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	if (swapchain->image_count == 0) {
		return;
	}

	VkCommandBuffer cmd_bufs[swapchain->image_count];
	for(uint32_t i = 0; i < swapchain->image_count; i++) {
		struct wlr_vk_swapchain_buffer *buf = &swapchain->buffers[i];
		if(buf->framebuffer) {
			vkDestroyFramebuffer(vulkan->dev, buf->framebuffer, NULL);
		}

		if(buf->image_view) {
			vkDestroyImageView(vulkan->dev, buf->image_view, NULL);
		}

		if(buf->cmdbuf) {
			cmd_bufs[i] = buf->cmdbuf;
		}
	}

	vkFreeCommandBuffers(vulkan->dev, swapchain->renderer->command_pool,
		swapchain->image_count, cmd_bufs);

	swapchain->image_count = 0;
	free(swapchain->buffers);
}

static bool init_swapchain_buffers(struct wlr_vk_swapchain *swapchain) {
	VkResult res;
	struct wlr_vk_renderer *renderer = swapchain->renderer;
	struct wlr_vulkan *vulkan = renderer->vulkan;

	destroy_swapchain_buffers(swapchain);
	res = vkGetSwapchainImagesKHR(vulkan->dev, swapchain->swapchain,
		&swapchain->image_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to get swapchain images (1)", res);
		return false;
	}

	VkImage images[swapchain->image_count];
	res = vkGetSwapchainImagesKHR(vulkan->dev, swapchain->swapchain,
		&swapchain->image_count, images);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to get swapchain images (2)", res);
		return false;
	}

	if (!(swapchain->buffers = calloc(swapchain->image_count,
				sizeof(*swapchain->buffers)))) {
		wlr_log(WLR_ERROR, "Failed to allocate swapchain buffers");
		return false;
	}

	VkCommandBuffer cmd_bufs[swapchain->image_count];
	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = swapchain->image_count;

	res = vkAllocateCommandBuffers(vulkan->dev, &cmd_buf_info, cmd_bufs);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		return false;
	}

	for (uint32_t i = 0; i < swapchain->image_count; i++) {
		struct wlr_vk_swapchain_buffer *buf = &swapchain->buffers[i];

		VkImageViewCreateInfo view_info = {0};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.pNext = NULL;
		view_info.format = swapchain->create_info.imageFormat;
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_G;
		view_info.components.b = VK_COMPONENT_SWIZZLE_B;
		view_info.components.a = VK_COMPONENT_SWIZZLE_A;
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_info.subresourceRange.baseMipLevel = 0;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.baseArrayLayer = 0;
		view_info.subresourceRange.layerCount = 1;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.flags = 0;
		view_info.image = images[i];

		buf[i].image = images[i];
		res = vkCreateImageView(vulkan->dev, &view_info, NULL,
			&swapchain->buffers[i].image_view);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateImageView", res);
			return false;
		}

		VkFramebufferCreateInfo fb_info = {0};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments = &buf->image_view;
		fb_info.renderPass = renderer->render_pass;
		fb_info.width = swapchain->create_info.imageExtent.width;
		fb_info.height = swapchain->create_info.imageExtent.height;
		fb_info.layers = 1;

		res = vkCreateFramebuffer(vulkan->dev, &fb_info, NULL,
			&buf[i].framebuffer);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateFramebuffer", res);
			return false;
		}

		buf->cmdbuf = cmd_bufs[i];
	}

	return true;
}

void wlr_vk_swapchain_destroy(struct wlr_vk_swapchain *swapchain) {
	if (!swapchain || !swapchain->renderer) {
		return;
	}

	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	destroy_swapchain_buffers(swapchain);
	if (swapchain->swapchain) {
		vkDestroySwapchainKHR(vulkan->dev, swapchain->swapchain, NULL);
	}

	free(swapchain);
}

struct wlr_vk_swapchain *wlr_swapchain_create(struct wlr_vk_renderer *renderer,
		VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) {
	VkResult res;
	struct wlr_vulkan *vulkan = renderer->vulkan;
	struct wlr_vk_swapchain *swapchain;
	if (!(swapchain = calloc(1, sizeof(*swapchain)))) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_swapchain");
		return NULL;
	}

	swapchain->renderer = renderer;

	VkSwapchainCreateInfoKHR info = {0};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = surface;

	VkSurfaceCapabilitiesKHR caps;
	res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->phdev, surface,
		&caps);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("failed retrieve surface caps", res);
		return NULL;
	}

	if (caps.currentExtent.width == (uint32_t)-1) {
		info.imageExtent.width = width;
		info.imageExtent.height = height;
	} else {
		info.imageExtent.width = caps.currentExtent.width;
		info.imageExtent.height = caps.currentExtent.height;
	}

	// format
	uint32_t formats_count;
	res = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->phdev, surface,
		&formats_count, NULL);
	if(res != VK_SUCCESS || formats_count == 0) {
		wlr_vulkan_error("failed retrieve surface formats", res);
		return NULL;
	}

	VkSurfaceFormatKHR *formats = calloc(formats_count, sizeof(*formats));
	if (!formats) {
		wlr_log(WLR_ERROR, "allocation failed");
		return NULL;
	}

	res = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->phdev, surface,
		&formats_count, formats);
	if(res != VK_SUCCESS) {
		wlr_vulkan_error("failed retrieve surface formats", res);
		return NULL;
	}

	// TODO: srgb or unorm?
	// try to find a format matching our needs if we don't have
	// free choice
	info.imageFormat = formats[0].format;
	info.imageColorSpace = formats[0].colorSpace;

	if (formats_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
		info.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
	}

	free(formats);

	// Get available present modes
	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->phdev, surface,
		&present_mode_count, NULL);

	VkPresentModeKHR *present_modes =
		calloc(present_mode_count, sizeof(VkPresentModeKHR));
	if (!present_modes) {
		wlr_log(WLR_ERROR, "allocation failed");
		return NULL;
	}

	res = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->phdev, surface,
		&present_mode_count, present_modes);
	if (res != VK_SUCCESS || present_mode_count == 0) {
		wlr_vulkan_error("Failed to retrieve surface present modes", res);
		return NULL;
	}

	info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

	if (!vsync) {
		for (size_t i = 0; i < present_mode_count; i++) {
			if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
				info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			} if ((info.presentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
					(present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
				info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
	}

	free(present_modes);

	uint32_t pref_image_count = caps.minImageCount + 1;
	if ((caps.maxImageCount > 0) && (pref_image_count > caps.maxImageCount)) {
		pref_image_count = caps.maxImageCount;
	}

	// Find the transformation of the surface
	VkSurfaceTransformFlagBitsKHR transform =
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
		transform = caps.currentTransform;
	}

	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkCompositeAlphaFlagBitsKHR alpha_flags[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
	};

	for (int i = 0; i < 4; ++i) {
		if (caps.supportedCompositeAlpha & alpha_flags[i]) {
			alpha = alpha_flags[i];
			break;
		}
	}

	info.minImageCount = pref_image_count;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.preTransform = transform;
	info.imageArrayLayers = 1;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.clipped = VK_TRUE;
	info.compositeAlpha = alpha;

	res = vkCreateSwapchainKHR(vulkan->dev, &info, NULL, &swapchain->swapchain);
	if (res != VK_SUCCESS || !swapchain->swapchain) {
		wlr_vulkan_error("Failed to create vk swapchain", res);
		return NULL;
	}

	if (!init_swapchain_buffers(swapchain)) {
		goto error;
	}

	return swapchain;

error:
	wlr_vk_swapchain_destroy(swapchain);
	return NULL;
}

static uint32_t clamp(uint32_t val, uint32_t low, uint32_t high) {
	return (val < low) ? low : ((val > high) ? high : val);
}

bool wlr_vk_swapchain_resize(struct wlr_vk_swapchain *swapchain,
		uint32_t width, uint32_t height) {

	VkResult res;
	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	VkSurfaceCapabilitiesKHR caps;
	res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->phdev,
		swapchain->surface, &caps);
	if(res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to retrieve surface caps", res);
		return false;
	}

	VkExtent2D ex = caps.currentExtent;
	if(ex.width == 0xFFFFFFFF && ex.height == 0xFFFFFFFF) {
		swapchain->create_info.imageExtent.width = clamp(width,
			caps.minImageExtent.width, caps.maxImageExtent.width);
		swapchain->create_info.imageExtent.height = clamp(height,
			caps.minImageExtent.height, caps.maxImageExtent.height);
	} else {
		swapchain->create_info.imageExtent = ex;
	}

	swapchain->create_info.oldSwapchain = swapchain->swapchain;
	res = vkCreateSwapchainKHR(vulkan->dev, &swapchain->create_info, NULL,
		&swapchain->swapchain);
	if(res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateSwapchainKHR", res);
		return false;
	}

	if(swapchain->create_info.oldSwapchain) {
		vkDestroySwapchainKHR(vulkan->dev, swapchain->create_info.oldSwapchain,
			NULL);
		swapchain->create_info.oldSwapchain = VK_NULL_HANDLE;
	}

	if (!init_swapchain_buffers(swapchain)) {
		return false;
	}

	return true;
}

struct wlr_vulkan *wlr_vulkan_create(
		unsigned int ini_ext_count, const char **ini_exts,
		unsigned int dev_ext_count, const char **dev_exts,
		bool debug) {
	struct wlr_vulkan *vulkan;
	if (!(vulkan = calloc(1, sizeof(*vulkan)))) {
		wlr_log_errno(WLR_ERROR, "failed to allocate wlr_vulkan");
		return NULL;
	}

	if (!init_instance(vulkan, ini_ext_count, ini_exts, debug)) {
		wlr_log(WLR_ERROR, "Could not init vulkan instance");
		goto error;
	}

	if (!init_device(vulkan, dev_ext_count, dev_exts)) {
		wlr_log(WLR_ERROR, "Could not init vulkan device");
		goto error;
	}

	return vulkan;

error:
	wlr_vulkan_destroy(vulkan);
	return NULL;
}
