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

	// some of the warnings are not helpful
	static const char *const ignored[] = {
		// warning if clear is used before any other command
		"UNASSIGNED-CoreValidation-DrawState-ClearCmdBeforeDraw",
		"UNASSIGNED-CoreValidation-Shader-OutputNotConsumed",
	};

	if (debug_data->pMessageIdName) {
		for(unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) {
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
	const char *missing = find_extensions(avail_ext_props, avail_extc,
		req_exts, req_extc);
	if (missing) {
		wlr_log(WLR_ERROR, "Instance extension %s unsupported", missing);
		free(avail_ext_props);
		return false;
	}

	bool debug_utils_found = false;
	unsigned last_ext = req_extc - 1;
	const char *extensions[req_extc + 4];
	memcpy(extensions, req_exts, req_extc * sizeof(*req_exts));

	const char *name = VK_KHR_SURFACE_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		extensions[++last_ext] = name;

		name = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			extensions[++last_ext] = name;
			vulkan->extensions.wayland = true;
		}

#ifdef WLR_HAS_X11_BACKEND
		name = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			extensions[++last_ext] = name;
			vulkan->extensions.xcb = true;
		}
#endif
	} else {
		wlr_log(WLR_INFO, "vk_khr_surface not supported");
	}

	if (debug) {
		const char *name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			debug_utils_found = true;
			extensions[++last_ext] = name;
		}
	}

	free(avail_ext_props);

	// NOTE: we could use a compositor name, version provided to us here
	VkApplicationInfo application_info = {0};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "wlroots-compositor";
	application_info.applicationVersion = 1;
	application_info.pEngineName = "wlroots";
	application_info.engineVersion = WLR_VERSION_NUM;
	application_info.apiVersion = VK_MAKE_VERSION(1,1,0);

	// standard_validation: reports error in api usage to debug callback
	// renderdoc: allows to capture (and debug) frames with renderdoc
	//   renderdoc has problems with some extensions we use atm so
	//   does not work
	const char* layers[] = {
		"VK_LAYER_LUNARG_standard_validation",
		// "VK_LAYER_RENDERDOC_Capture",
	};
	unsigned layer_count = debug * (sizeof(layers) / sizeof(layers[0]));

	VkInstanceCreateInfo instance_info = {0};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application_info;
	instance_info.enabledExtensionCount = last_ext + 1;
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
		exts, ext_count);
	if (missing) {
		wlr_log(WLR_ERROR, "Device extension %s unsupported", missing);
		free(avail_ext_props);
		return NULL;
	}

	unsigned last_ext = ext_count - 1;
	const char *extensions[ext_count + 2u];
	memcpy(extensions, exts, ext_count * sizeof(*exts));

	const char *name = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		extensions[++last_ext] = name;
	} else {
		wlr_log(WLR_INFO, "vk_khr_swapchain extension not supported");
		vulkan->extensions.wayland = false;
		vulkan->extensions.xcb = false;
	}

	name = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		extensions[++last_ext] = name;
		vulkan->extensions.external_mem_fd = true;

		name = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			extensions[++last_ext] = name;
			vulkan->extensions.dmabuf = true;
		} else {
			wlr_log(WLR_INFO, "vk dmabuf extension not supported");
			vulkan->extensions.dmabuf = false;
		}
	} else {
		wlr_log(WLR_INFO, "vk_khr_external_memory_fd extension not supported");
		vulkan->extensions.external_mem_fd = false;
		vulkan->extensions.dmabuf = false;
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
	dev_info.enabledExtensionCount = last_ext + 1;
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
