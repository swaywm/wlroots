#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <render/vulkan.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include <wlr/config.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#ifdef WLR_HAS_X11_BACKEND
	#include <xcb/xcb.h>
	#include <vulkan/vulkan_xcb.h>
#endif

// Returns the name of the first extension that could not be found or NULL.
static const char *find_extensions(const VkExtensionProperties *avail,
		unsigned availc, const char **req, unsigned reqc) {
	// check if all required extensions are supported
	for (size_t i = 0; i < reqc; ++i) {
		bool found = false;
		for (size_t j = 0; j < availc; ++j) {
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

static VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT *debug_data,
		void *data) {

	((void) data);

	// we ignore some of the non-helpful warnings
	static const char *const ignored[] = {
		// if clear is used before any other command
		"UNASSIGNED-CoreValidation-DrawState-ClearCmdBeforeDraw",
		// notifies us that shader output is not consumed since
		// we use the shared vertex buffer with uv output
		"UNASSIGNED-CoreValidation-Shader-OutputNotConsumed",
	};

	if (debug_data->pMessageIdName) {
		for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) {
			if (!strcmp(debug_data->pMessageIdName, ignored[i])) {
				return false;
			}
		}
	}

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

	wlr_log(importance, "%s (%s)", debug_data->pMessage,
		debug_data->pMessageIdName);
	if (debug_data->queueLabelCount > 0) {
		const char *name = debug_data->pQueueLabels[0].pLabelName;
		if (name) {
			wlr_log(importance, "    last label '%s'", name);
		}
	}

	for (unsigned i = 0; i < debug_data->objectCount; ++i) {
		if (debug_data->pObjects[i].pObjectName) {
			wlr_log(importance, "    involving '%s'", debug_data->pMessage);
		}
	}

	return false;
}


// instance
struct wlr_vk_instance *wlr_vk_instance_create(
		unsigned ext_count, const char **exts, bool debug,
		const char *compositor_name, unsigned compositor_version) {

	// query extension support
	uint32_t avail_extc = 0;
	VkResult res;
	res = vkEnumerateInstanceExtensionProperties(NULL, &avail_extc, NULL);
	if ((res != VK_SUCCESS) || (avail_extc == 0)) {
		wlr_vk_error("Could not enumerate instance extensions (1)", res);
		return NULL;
	}

	VkExtensionProperties avail_ext_props[avail_extc + 1];
	res = vkEnumerateInstanceExtensionProperties(NULL, &avail_extc,
		avail_ext_props);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Could not enumerate instance extensions (2)", res);
		return NULL;
	}

	for (size_t j = 0; j < avail_extc; ++j) {
		wlr_log(WLR_INFO, "Vulkan Instance extensions %s",
			avail_ext_props[j].extensionName);
	}

	// create instance
	struct wlr_vk_instance *ini = calloc(1, sizeof(*ini));
	if (!ini) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		return NULL;
	}

	wl_list_init(&ini->devices);
	bool debug_utils_found = false;
	ini->extensions = calloc(6 + ext_count, sizeof(*ini->extensions));
	if (!ini->extensions) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		goto error;
	}

	// find extensions
	for (unsigned i = 0; i < ext_count; ++i) {
		if (find_extensions(avail_ext_props, avail_extc, &exts[i], 1)) {
			wlr_log(WLR_DEBUG, "vulkan instance extension %s not found",
				exts[i]);
			continue;
		}

		ini->extensions[ini->extension_count++] = exts[i];
	}

	const char *name = VK_KHR_SURFACE_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		ini->extensions[ini->extension_count++] = name;

		name = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			ini->extensions[ini->extension_count++] = name;
		}

		name = VK_KHR_DISPLAY_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			ini->extensions[ini->extension_count++] = name;

			name = VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME;
			if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
				ini->extensions[ini->extension_count++] = name;
			}
		}

#ifdef WLR_HAS_X11_BACKEND
		name = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			ini->extensions[ini->extension_count++] = name;
		}
#endif
	} else {
		wlr_log(WLR_DEBUG, "VK_KHR_SURFACE not supported");
	}

	if (debug) {
		const char *name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			debug_utils_found = true;
			ini->extensions[ini->extension_count++] = name;
		}
	}

	VkApplicationInfo application_info = {0};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = compositor_name;
	application_info.applicationVersion = compositor_version;
	application_info.pEngineName = "wlroots";
	application_info.engineVersion = WLR_VERSION_NUM;
	application_info.apiVersion = VK_MAKE_VERSION(1,1,0);

	// standard_validation: reports error in api usage to debug callback
	// renderdoc: allows to capture (and debug) frames with renderdoc
	//   renderdoc has problems with some extensions we use atm so
	//   does not work
	const char *layers[] = {
		"VK_LAYER_LUNARG_standard_validation",
		// "VK_LAYER_RENDERDOC_Capture",
	};
	unsigned layer_count = debug * (sizeof(layers) / sizeof(layers[0]));

	VkInstanceCreateInfo instance_info = {0};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application_info;
	instance_info.enabledExtensionCount = ini->extension_count;
	instance_info.ppEnabledExtensionNames = ini->extensions;
	instance_info.enabledLayerCount = layer_count;
	instance_info.ppEnabledLayerNames = layers;

	res = vkCreateInstance(&instance_info, NULL, &ini->instance);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Could not create instance", res);
		goto error;
	}

	// debug callback
	if (debug_utils_found) {
		ini->api.createDebugUtilsMessengerEXT =
			(PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					ini->instance, "vkCreateDebugUtilsMessengerEXT");
		ini->api.destroyDebugUtilsMessengerEXT =
			(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					ini->instance, "vkDestroyDebugUtilsMessengerEXT");

		if (ini->api.createDebugUtilsMessengerEXT) {
			VkDebugUtilsMessageSeverityFlagsEXT severity =
				// VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			VkDebugUtilsMessageTypeFlagsEXT types =
				// VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

			VkDebugUtilsMessengerCreateInfoEXT debug_info = {0};
			debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debug_info.messageSeverity = severity;
			debug_info.messageType = types;
			debug_info.pfnUserCallback = &debug_callback;
			debug_info.pUserData = ini;

			ini->api.createDebugUtilsMessengerEXT(ini->instance,
				&debug_info, NULL, &ini->messenger);
		} else {
			wlr_log(WLR_ERROR, "vkCreateDebugUtilsMessengerEXT not found");
		}
	}

	return ini;

error:
	wlr_vk_instance_destroy(ini);
	return NULL;
}

void wlr_vk_instance_destroy(struct wlr_vk_instance *ini) {
	if (!ini) {
		return;
	}

	struct wlr_vk_device *dev, *tmp;
	wl_list_for_each_safe(dev, tmp, &ini->devices, link) {
		wlr_vk_device_destroy(dev);
	}

	if (ini->messenger && ini->api.destroyDebugUtilsMessengerEXT) {
		ini->api.destroyDebugUtilsMessengerEXT(ini->instance,
			ini->messenger, NULL);
	}

	if (ini->instance) {
		vkDestroyInstance(ini->instance, NULL);
	}

	free(ini->extensions);
	free(ini);
}

// device
struct wlr_vk_device *wlr_vk_device_create(struct wlr_vk_instance *ini,
		VkPhysicalDevice phdev, unsigned ext_count, const char **exts,
		unsigned queue_count, struct wlr_vk_queue *queue_families) {
	VkResult res;
	const char *name;

	// check for extensions
	uint32_t avail_extc = 0;
	res = vkEnumerateDeviceExtensionProperties(phdev, NULL,
		&avail_extc, NULL);
	if ((res != VK_SUCCESS) || (avail_extc == 0)) {
		wlr_vk_error("Could not enumerate device extensions (1)", res);
		return NULL;
	}

	VkExtensionProperties avail_ext_props[avail_extc + 1];
	res = vkEnumerateDeviceExtensionProperties(phdev, NULL,
		&avail_extc, avail_ext_props);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Could not enumerate device extensions (2)", res);
		return NULL;
	}

	for (size_t j = 0; j < avail_extc; ++j) {
		wlr_log(WLR_INFO, "Vulkan Device extension %s",
			avail_ext_props[j].extensionName);
	}

	// create device
	struct wlr_vk_device *dev = calloc(1, sizeof(*dev));
	if (!dev) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		return NULL;
	}

	wl_list_init(&dev->link);
	dev->phdev = phdev;
	dev->instance = ini;
	dev->extensions = calloc(5 + ext_count, sizeof(*ini->extensions));
	if (!dev->extensions) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		goto error;
	}

	// find extensions
	for (unsigned i = 0; i < ext_count; ++i) {
		if (find_extensions(avail_ext_props, avail_extc, &exts[i], 1)) {
			wlr_log(WLR_DEBUG, "vulkan device extension %s not found",
				exts[i]);
			continue;
		}

		dev->extensions[dev->extension_count++] = exts[i];
	}

	name = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		dev->extensions[dev->extension_count++] = name;

		name = VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			dev->extensions[dev->extension_count++] = name;
		}
	} else {
		wlr_log(WLR_INFO, "VK_KHR_SWAPCHAIN not supported");
	}

	name = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		dev->extensions[dev->extension_count++] = name;

		name = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			dev->extensions[dev->extension_count++] = name;
		} else {
			wlr_log(WLR_INFO, "VK_EXT_EXTERNAL_MEMORY_DMA_BUF not supported");
		}
	} else {
		wlr_log(WLR_INFO, "VK_KHR_EXTERNAL_MEMORY_FD not supported");
	}

	name = VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		dev->extensions[dev->extension_count++] = name;
	} else {
		wlr_log(WLR_INFO, "vk_khr_increment_present extension not supported");
	}

	{
		float prio = 1.f;
		VkDeviceQueueCreateInfo qinfos[queue_count];
		memset(qinfos, 0, sizeof(qinfos));
		for (unsigned i = 0u; i < queue_count; ++i) {
			qinfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			qinfos[i].queueFamilyIndex = queue_families[i].family;
			qinfos[i].queueCount = 1;
			qinfos[i].pQueuePriorities = &prio;
		}

		VkDeviceCreateInfo dev_info = {0};
		dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		dev_info.queueCreateInfoCount = queue_count;
		dev_info.pQueueCreateInfos = qinfos;
		dev_info.enabledExtensionCount = dev->extension_count;
		dev_info.ppEnabledExtensionNames = dev->extensions;

		res = vkCreateDevice(phdev, &dev_info, NULL, &dev->dev);
		if (res != VK_SUCCESS){
			wlr_log(WLR_ERROR, "Failed to create vulkan device");
			goto error;
		}
	}

	for (unsigned i = 0u; i < queue_count; ++i) {
		vkGetDeviceQueue(dev->dev, queue_families[i].family, 0,
			&queue_families[i].queue);
	}

	dev->queue_count = queue_count;
	dev->queues = calloc(queue_count, sizeof(*dev->queues));
	if (!dev->queues) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
	}

	memcpy(dev->queues, queue_families, sizeof(*dev->queues) * queue_count);

	// load api
	dev->api.getMemoryFdPropertiesKHR =
		(PFN_vkGetMemoryFdPropertiesKHR) vkGetDeviceProcAddr(
				dev->dev, "vkGetMemoryFdPropertiesKHR");
	if (!dev->api.getMemoryFdPropertiesKHR) {
		wlr_log(WLR_ERROR, "Failed to retrieve required dev function pointers");
		goto error;
	}

	wl_list_insert(&ini->devices, &dev->link);
	return dev;

error:
	wlr_vk_device_destroy(dev);
	return NULL;
}

void wlr_vk_device_destroy(struct wlr_vk_device *dev) {
	if (!dev) {
		return;
	}

	if (dev->dev) {
		vkDestroyDevice(dev->dev, NULL);
	}

	wl_list_remove(&dev->link);
	free(dev->extensions);
	free(dev);
}

bool wlr_vk_present_queue_supported_xcb(struct wlr_vk_instance *instance,
		uintptr_t vk_physical_device, uint32_t qfam,
		void *xcb_connection, uint32_t visualid) {
#ifdef WLR_HAS_X11_BACKEND
	if (!vulkan_has_extension(instance->extension_count, instance->extensions,
			VK_KHR_XCB_SURFACE_EXTENSION_NAME)) {
		return false;
	}

	VkPhysicalDevice phdev = (VkPhysicalDevice) vk_physical_device;
	return vkGetPhysicalDeviceXcbPresentationSupportKHR(phdev, qfam,
		xcb_connection, visualid);
#else
	return false;
#endif
}

bool wlr_vk_present_queue_supported_wl(struct wlr_vk_instance *instance,
		uintptr_t vk_physical_device, uint32_t qfam,
		struct wl_display *remote) {
	if (!vulkan_has_extension(instance->extension_count, instance->extensions,
			VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
		return false;
	}

	VkPhysicalDevice phdev = (VkPhysicalDevice) vk_physical_device;
	return vkGetPhysicalDeviceWaylandPresentationSupportKHR(phdev, qfam,
		remote);
}
