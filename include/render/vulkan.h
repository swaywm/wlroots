#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/interface.h>

// State (e.g. image texture) associates with a surface.
struct wlr_vulkan_texture {
	struct wlr_texture wlr_texture;
	struct wlr_vulkan *vulkan;
	VkDeviceMemory memory;
	VkImage image;
	VkImageView image_view;
	const struct wlr_vulkan_pixel_format *format;
	unsigned width;
	unsigned height;
	bool has_alpha;
};

// Central vulkan state
struct wlr_vulkan {
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;
	VkPhysicalDevice phdev;
	VkDevice dev;

	VkQueue graphics_queue;
	VkQueue present_queue;
	unsigned graphics_queue_fam;
	unsigned present_queue_fam;

	struct {
		PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT;
		PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT;
	} api;
};

// One buffer (image texture + command buffer) of a swapchain.
struct wlr_vk_swapchain_buffer {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	VkCommandBuffer cmdbuf;
};

// Vulkan swapchain with retrieved buffers and rendering data.
struct wlr_vk_swapchain {
	struct wlr_vk_renderer *renderer;
	VkSwapchainCreateInfoKHR create_info;
	VkSwapchainKHR swapchain;
	VkSurfaceKHR surface;

	unsigned int image_count;
	struct wlr_vk_swapchain_buffer* buffers;
};

// Vulkan renderer on top of wlr_vulkan able to render onto swapchains.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_vulkan* vulkan;
	VkSampler sampler;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkRenderPass render_pass;

	VkPipeline pipeline;
	VkCommandPool command_pool;
	VkDescriptorSet descriptor_set;
	VkDescriptorPool descriptor_pool;
	VkDeviceMemory memory;
	VkBuffer ubo;

	// struct wl_global *wl_drm;
};

struct wlr_vulkan_pixel_format {
	uint32_t wl_format;
	VkFormat vk_format;
	int bpp;
	bool has_alpha;
};

// Creates a swapchain for the given surface.
struct wlr_vk_swapchain *wlr_vk_swapchain_create(
		struct wlr_vk_renderer *renderer, VkSurfaceKHR surface,
		uint32_t width, uint32_t height, bool vsync);
bool wlr_vk_swapchain_resize(struct wlr_vk_swapchain *swapchain,
		uint32_t width, uint32_t height);
void wlr_vk_swapchain_destroy(struct wlr_vk_swapchain *swapchain);

// Initializes a wlr_vulkan state. This will require
// the given extensions and fail if they cannot be found.
// Will additionally require the swapchain and surface base extensions.
// param debug: Whether to enable debug layers and create a debug callback.
struct wlr_vulkan *wlr_vulkan_create(
		unsigned int ini_ext_count, const char **ini_exts,
		unsigned int dev_ext_count, const char **dev_exts,
		bool debug);

void wlr_vulkan_destroy(struct wlr_vulkan *vulkan);
int wlr_vulkan_find_mem_type(struct wlr_vulkan *vulkan,
	VkMemoryPropertyFlags flags, uint32_t req_bits);

const enum wl_shm_format *get_vulkan_formats(size_t *len);
const struct wlr_vulkan_pixel_format *get_vulkan_format_from_wl(
	enum wl_shm_format fmt);

struct wlr_vulkan_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);
const char *vulkan_strerror(VkResult err);

#endif

