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
struct wlr_vk_texture {
	struct wlr_texture wlr_texture;
	struct wlr_vk_renderer *renderer;
	VkDeviceMemory memory;
	VkImage image;
	VkImageView image_view;
	const struct wlr_vk_pixel_format *format;
	uint32_t width;
	uint32_t height;
	bool has_alpha;
	VkDescriptorSet ds;
	VkSubresourceLayout subres_layout;
};

// Central vulkan state
struct wlr_vulkan {
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;
	VkPhysicalDevice phdev;
	VkDevice dev;

	VkQueue graphics_queue;
	VkQueue present_queue;
	uint32_t graphics_queue_fam;
	uint32_t present_queue_fam;

	struct {
		PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT;
		PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT;
		PFN_vkGetMemoryFdPropertiesKHR getMemoryFdPropertiesKHR;
	} api;
};

// One buffer of a swapchain.
// Add a VkCommandBuffer when we don't record every frame anymore.
struct wlr_vk_swapchain_buffer {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	int age;
};

// Vulkan swapchain with retrieved buffers and rendering data.
struct wlr_vk_swapchain {
	struct wlr_vk_renderer *renderer;
	VkSwapchainCreateInfoKHR create_info;
	VkSwapchainKHR swapchain;
	VkSurfaceKHR surface;

	bool readable;
	uint32_t image_count;
	struct wlr_vk_swapchain_buffer* buffers;
};

// Vulkan renderer on top of wlr_vulkan able to render onto swapchains.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;
	struct wlr_vulkan *vulkan;

	VkCommandPool command_pool;
	VkRenderPass render_pass;
	VkSampler sampler;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkDescriptorPool descriptor_pool;

	struct wlr_vk_render_surface *current;
};

struct wlr_vk_pixel_format {
	uint32_t wl_format;
	VkFormat vk_format;
	int bpp;
	bool has_alpha;
};

struct wlr_vk_render_surface {
	struct wlr_render_surface render_surface;
	struct wlr_vk_renderer *renderer;
	uint32_t width;
	uint32_t height;
	VkCommandBuffer cb; // the currently recorded cb
};

// wlr_vk_render_surface using a VkSurfaceKHR and swapchain
struct wlr_vk_swapchain_render_surface {
	struct wlr_vk_render_surface vk_render_surface;
	VkSurfaceKHR surface;
	struct wlr_vk_swapchain swapchain;
	uint32_t current_id;

	VkSemaphore acquire; // signaled when image was acquire
	VkSemaphore present; // signaled when rendering finished
};

// wlr_vk_render_surface implementing an own offscreen swapchain
// might be created with a gbm device, allowing it to return a gbm_bo
// for the current front buffer
struct wlr_vk_offscreen_render_surface {
	struct wlr_vk_render_surface vk_render_surface;
	struct wlr_drm_surface *drm_surface;
	struct wlr_vk_offscreen_buffer {
		struct gbm_bo *bo; // optional
		VkDeviceMemory memory;
		struct wlr_vk_swapchain_buffer buffer;
		int age;// initially 0, then always 2 (to track never used buffers)
	} buffers[2];

	struct wlr_vk_offscreen_buffer *front; // presented, not renderable
	struct wlr_vk_offscreen_buffer *back; // rendered to in current/next frame
};

// Creates a swapchain for the given surface.
bool wlr_vk_swapchain_init(struct wlr_vk_swapchain *swapchain,
		struct wlr_vk_renderer *renderer, VkSurfaceKHR surface,
		uint32_t width, uint32_t height, bool vsync);
bool wlr_vk_swapchain_resize(struct wlr_vk_swapchain *swapchain,
		uint32_t width, uint32_t height);
void wlr_vk_swapchain_finish(struct wlr_vk_swapchain *swapchain);

// Initializes a wlr_vulkan state. This will require
// the given extensions and fail if they cannot be found.
// Will automatically require the swapchain and surface base extensions.
// param debug: Whether to enable debug layers and create a debug callback.
struct wlr_vulkan *wlr_vulkan_create(
		unsigned ini_ext_count, const char **ini_exts,
		unsigned dev_ext_count, const char **dev_exts,
		bool debug);

void wlr_vulkan_destroy(struct wlr_vulkan *vulkan);
int wlr_vulkan_find_mem_type(struct wlr_vulkan *vulkan,
	VkMemoryPropertyFlags flags, uint32_t req_bits);

const enum wl_shm_format *get_vulkan_formats(size_t *len);
const struct wlr_vk_pixel_format *get_vulkan_format_from_wl(
	enum wl_shm_format fmt);

struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *wlr_renderer);
struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);
const char *vulkan_strerror(VkResult err);

#endif

