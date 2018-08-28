#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <render/vulkan.h>
#include <wlr/util/log.h>
#include <wlr/version.h>

#define wlr_vulkan_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

void wlr_vulkan_destroy(struct wlr_vulkan *vulkan) {
	if (!vulkan) {
		return;
	}

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

	((void) data);

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

	if(type == VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
		importance = WLR_DEBUG;
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

// Returns the name of the first extension that could not be found or NULL.
static const char *find_extensions(const VkExtensionProperties *avail,
		unsigned availc, const char **req, unsigned reqc) {
	// check if all required extensions are supported
	for(size_t i = 0; i < reqc; ++i) {
		bool found = false;
		for(size_t j = 0; j < availc; ++j) {
			if (!strcmp(avail[j].extensionName, req[i])) {
				found = true;
				break;
			}
		}

		if (!found) {
			return req[i];
		}
	}

	return NULL;
}

static bool init_instance(struct wlr_vulkan *vulkan,
		unsigned int req_extc, const char **req_exts, bool debug) {
	uint32_t avail_extc = 0;
	VkResult res;
	res = vkEnumerateInstanceExtensionProperties(NULL, &avail_extc, NULL);
	if ((res != VK_SUCCESS) || (avail_extc == 0)) {
		wlr_vulkan_error("Could not enumerate instance extensions (1)", res);
		return false;
	}

	VkExtensionProperties *avail_ext_props =
		calloc(avail_extc, sizeof(VkExtensionProperties));
	if (!avail_ext_props) {
		wlr_log(WLR_ERROR, "allocation failed");
		return false;
	}

	res = vkEnumerateInstanceExtensionProperties(NULL, &avail_extc,
		avail_ext_props);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Could not enumerate instance extensions (2)", res);
		free(avail_ext_props);
		return false;
	}

	// output all extensions
	for(size_t j = 0; j < avail_extc; ++j) {
		wlr_log(WLR_INFO, "Vulkan Instance extensions %s",
			avail_ext_props[j].extensionName);
	}

	// try to find required/optional ones
	bool debug_utils_found = debug;
	unsigned extension_count = req_extc + 1 + debug;
	const char *extensions[extension_count];
	memcpy(extensions, req_exts, req_extc * sizeof(*req_exts));
	extensions[req_extc + 0] = VK_KHR_SURFACE_EXTENSION_NAME;
	if (debug) {
		const char *name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		extensions[req_extc + 1] = name;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1)) {
			debug_utils_found = false;
			extension_count--;
		}
	}

	const char *missing = find_extensions(avail_ext_props, avail_extc,
		extensions, extension_count);
	if (missing) {
		wlr_log(WLR_ERROR, "Instance extension %s unsupported", missing);
		free(avail_ext_props);
		return NULL;
	}

	free(avail_ext_props);

	// TODO: use compositor version and name provided somewhere?
	VkApplicationInfo application_info = {0};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "wlroots-compositor";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "wlroots";
	application_info.engineVersion = WLR_VERSION_NUM;
	application_info.apiVersion = VK_MAKE_VERSION(1,1,0);

	// standard_validation: reports error in api usage to debug callback
	// renderdoc: allows to capture (and debug!) frames with renderdoc
	const char* layers[] = {
		"VK_LAYER_LUNARG_standard_validation",
		"VK_LAYER_RENDERDOC_Capture"
	};
	unsigned layer_count = debug * (sizeof(layers) / sizeof(layers[0]));

	VkInstanceCreateInfo instance_info = {0};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application_info;
	instance_info.enabledExtensionCount = extension_count;
	instance_info.ppEnabledExtensionNames = extensions;
	instance_info.enabledLayerCount = layer_count;
	instance_info.ppEnabledLayerNames = layers;

	res = vkCreateInstance(&instance_info, NULL, &vulkan->instance);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Could not create instance", res);
		return false;
	}

	// debug callback
	if (debug_utils_found) {
		vulkan->api.createDebugUtilsMessengerEXT =
			(PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					vulkan->instance, "vkCreateDebugUtilsMessengerEXT");
		vulkan->api.destroyDebugUtilsMessengerEXT =
			(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					vulkan->instance, "vkDestroyDebugUtilsMessengerEXT");

		if(vulkan->api.createDebugUtilsMessengerEXT) {
			VkDebugUtilsMessageSeverityFlagsEXT severity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
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
	// query one based on
	//  - user preference (env variables/config)
	//  - supported presenting caps
	//  - supported extensions, external memory import properties
	//  - (as default, when both ok) integrated vs dedicated
	uint32_t num_devs = 1;
	VkResult res;
	res = vkEnumeratePhysicalDevices(vulkan->instance, &num_devs,
		&vulkan->phdev);
	if ((res != VK_SUCCESS && res != VK_INCOMPLETE) || !vulkan->phdev) {
		wlr_log(WLR_ERROR, "Could not retrieve physical device");
		return false;
	}

	unsigned extension_count = ext_count + 2u;
	const char *extensions[extension_count];
	memcpy(extensions, exts, ext_count * sizeof(*exts));
	extensions[ext_count + 0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	extensions[ext_count + 1] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;

	// check for extensions
	uint32_t avail_extc = 0;
	res = vkEnumerateDeviceExtensionProperties(vulkan->phdev, NULL,
		&avail_extc, NULL);
	if ((res != VK_SUCCESS) || (avail_extc == 0)) {
		wlr_vulkan_error("Could not enumerate device extensions (1)", res);
		return false;
	}

	VkExtensionProperties *avail_ext_props =
		calloc(avail_extc, sizeof(VkExtensionProperties));
	if (!avail_ext_props) {
		wlr_log(WLR_ERROR, "allocation failed");
		return false;
	}

	res = vkEnumerateDeviceExtensionProperties(vulkan->phdev, NULL,
		&avail_extc, avail_ext_props);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Could not enumerate device extensions (2)", res);
		free(avail_ext_props);
		return false;
	}

	// output all extensions
	for(size_t j = 0; j < avail_extc; ++j) {
		wlr_log(WLR_INFO, "Vulkan Device extension %s",
			avail_ext_props[j].extensionName);
	}

	const char *missing = find_extensions(avail_ext_props, avail_extc,
		extensions, extension_count);
	if (missing) {
		wlr_log(WLR_ERROR, "Device extension %s unsupported", missing);
		free(avail_ext_props);
		return NULL;
	}

	free(avail_ext_props);

	// queue families
	uint32_t qfam_count;
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan->phdev, &qfam_count, NULL);
	VkQueueFamilyProperties *queue_props = calloc(qfam_count,
		sizeof(*queue_props));
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

	VkDeviceCreateInfo dev_info = {0};
	dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dev_info.queueCreateInfoCount = one_queue ? 1 : 2;
	dev_info.pQueueCreateInfos = queue_infos;
	dev_info.enabledExtensionCount = extension_count;
	dev_info.ppEnabledExtensionNames = extensions;

	res = vkCreateDevice(vulkan->phdev, &dev_info, NULL, &vulkan->dev);
	if (res != VK_SUCCESS){
		wlr_log(WLR_ERROR, "Failed to create vulkan device");
		return false;
	}

	vkGetDeviceQueue(vulkan->dev, vulkan->graphics_queue_fam, 0,
		&vulkan->graphics_queue);
	vkGetDeviceQueue(vulkan->dev, vulkan->present_queue_fam, 0,
		&vulkan->present_queue);

	// load api
	vulkan->api.getMemoryFdPropertiesKHR =
		(PFN_vkGetMemoryFdPropertiesKHR) vkGetDeviceProcAddr(
				vulkan->dev, "vkGetMemoryFdPropertiesKHR");
	if (!vulkan->api.getMemoryFdPropertiesKHR) {
		wlr_log(WLR_ERROR, "Failed to retrieve required dev function pointers");
		return false;
	}

	// TODO: query external memory/image stuff
	// should probably be up at phdev selection
	// vkGetPhysicalDeviceImageFormatProperties2(vulkan->phdev, ...);

	return true;
}

static void destroy_swapchain_buffers(struct wlr_vk_swapchain *swapchain) {
	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	if (swapchain->image_count == 0) {
		return;
	}

	for(uint32_t i = 0; i < swapchain->image_count; i++) {
		struct wlr_vk_swapchain_buffer *buf = &swapchain->buffers[i];
		if(buf->framebuffer) {
			vkDestroyFramebuffer(vulkan->dev, buf->framebuffer, NULL);
		}

		if(buf->image_view) {
			vkDestroyImageView(vulkan->dev, buf->image_view, NULL);
		}
	}

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

		buf->image = images[i];
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
			&buf->framebuffer);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateFramebuffer", res);
			return false;
		}
	}

	return true;
}

void wlr_vk_swapchain_finish(struct wlr_vk_swapchain *swapchain) {
	if (!swapchain || !swapchain->renderer) {
		return;
	}

	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	destroy_swapchain_buffers(swapchain);
	if (swapchain->swapchain) {
		vkDestroySwapchainKHR(vulkan->dev, swapchain->swapchain, NULL);
	}

	memset(swapchain, 0, sizeof(*swapchain));
}

bool wlr_vk_swapchain_init(struct wlr_vk_swapchain *swapchain,
		struct wlr_vk_renderer *renderer,
		VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) {

	VkResult res;
	struct wlr_vulkan *vulkan = renderer->vulkan;
	swapchain->renderer = renderer;
	swapchain->surface = surface;

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

	// transformation
	VkSurfaceTransformFlagBitsKHR transform =
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
		transform = caps.currentTransform;
	}

	// alpha
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

	// usage
	assert(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
		swapchain->readable = true;
		info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	} else {
		wlr_log(WLR_INFO, "Created swapchain will not be readable");
	}

	// create
	info.minImageCount = pref_image_count;
	info.preTransform = transform;
	info.imageArrayLayers = 1;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.clipped = VK_TRUE;
	info.compositeAlpha = alpha;
	swapchain->create_info = info;

	res = vkCreateSwapchainKHR(vulkan->dev, &info, NULL, &swapchain->swapchain);
	if (res != VK_SUCCESS || !swapchain->swapchain) {
		wlr_vulkan_error("Failed to create vk swapchain", res);
		return NULL;
	}

	// buffers
	if (!init_swapchain_buffers(swapchain)) {
		goto error;
	}

	return swapchain;

error:
	wlr_vk_swapchain_finish(swapchain);
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
