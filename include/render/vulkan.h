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

// Central vulkan state: instance, device, extensions, api
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

	// which optional extensions could be loaded
	struct {
		bool wayland;
		bool xcb;
		bool import_dma;
		bool export_dma;
	} extensions;
};

// One buffer of a swapchain.
// Add a VkCommandBuffer when we don't record every frame anymore.
struct wlr_vk_swapchain_buffer {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	int age;
	bool layout_changed;
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
	struct {
		uint32_t stride;
		uint32_t width;
		uint32_t height;
		uint32_t src_x;
		uint32_t src_y;
		uint32_t dst_x;
		uint32_t dst_y;
		void *data;
	} read_pixels;
};

struct wlr_vk_pixel_format {
	uint32_t wl_format;
	VkFormat vk_format;
	int bpp;
	bool has_alpha;
};

struct wlr_vk_render_surface {
	struct wlr_render_surface rs;
	struct wlr_vk_renderer *renderer;
	VkCommandBuffer cb; // the currently recorded cb
};

// wlr_vk_render_surface using a VkSurfaceKHR and swapchain
struct wlr_vk_swapchain_render_surface {
	struct wlr_vk_render_surface vk_rs;
	VkSurfaceKHR surface;
	struct wlr_vk_swapchain swapchain;
	uint32_t current_id; // current or last rendered buffer id

	VkSemaphore acquire; // signaled when image was acquire
	VkSemaphore present; // signaled when rendering finished
};

// wlr_vk_render_surface implementing an own offscreen swapchain
// might be created with a gbm device, allowing it to return a gbm_bo
// for the current front buffer
struct wlr_vk_offscreen_render_surface {
	struct wlr_vk_render_surface vk_rs;
	struct gbm_device *gbm_dev;
	uint32_t flags;

	struct wlr_vk_offscreen_buffer {
		struct gbm_bo *bo; // optional
		VkDeviceMemory memory;
		struct wlr_vk_swapchain_buffer buffer;
	} buffers[2];

	struct wlr_vk_offscreen_buffer *front; // presented, not renderable
	struct wlr_vk_offscreen_buffer *back; // rendered to in current/next frame
};

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

// Tries to find any memory bit for the given vulkan device that
// supports the given flags and is set in req_bits (e.g. if memory
// type 2 is ok, (req_bits & (1 << 2)) must not be null.
// Set req_bits to 0xFFFFFFFF to allow all types.
int wlr_vulkan_find_mem_type(struct wlr_vulkan *vulkan,
	VkMemoryPropertyFlags flags, uint32_t req_bits);

const enum wl_shm_format *get_vulkan_formats(size_t *len);
const struct wlr_vk_pixel_format *get_vulkan_format_from_wl(
	enum wl_shm_format fmt);

struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *wlr_renderer);
struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);
struct wlr_vk_render_surface *vulkan_get_render_surface(
		struct wlr_render_surface *wlr_rs);

// render_surface api
struct wlr_render_surface *vulkan_render_surface_create_headless(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height);
struct wlr_render_surface *vulkan_render_surface_create_xcb(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	void *xcb_connection, uint32_t xcb_window);
struct wlr_render_surface *vulkan_render_surface_create_wl(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	struct wl_display *disp, struct wl_surface *surf);
struct wlr_render_surface *vulkan_render_surface_create_gbm(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	struct gbm_device *gbm_dev, uint32_t flags);

VkFramebuffer vulkan_render_surface_begin(struct wlr_vk_render_surface *rs,
	VkCommandBuffer cb);
bool vulkan_render_surface_end(struct wlr_vk_render_surface *rs,
	VkCommandBuffer cb);
bool vulkan_render_surface_readable(struct wlr_vk_render_surface *rs);
void vulkan_render_surface_read_pixels(struct wlr_vk_render_surface *rs,
	uint32_t stride, uint32_t width, uint32_t height,
	uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, void *data);


// util
const char *vulkan_strerror(VkResult err);
void vulkan_change_layout(VkCommandBuffer cb, VkImage img,
		VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
		VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta);

#define wlr_vulkan_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)


#endif

