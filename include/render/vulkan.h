#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/interface.h>

// State (e.g. image texture) associated with a surface.
struct wlr_vk_texture {
	struct wlr_texture wlr_texture;
	struct wlr_vk_renderer *renderer;
	VkDeviceMemory memory;
	VkImage image;
	VkImageView image_view;
	const struct wlr_vk_pixel_format *format;
	uint32_t width;
	uint32_t height;
	VkDescriptorSet ds;
	uint32_t last_written;
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

	// which optional extensions are available
	struct {
		bool wayland;
		bool xcb;
		bool external_mem_fd;
		bool dmabuf;
		bool incremental_present;
	} extensions;
};

// One buffer of a swapchain (VkSwapchainKHR or custom).
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

struct wlr_vk_allocation {
	VkDeviceSize start;
	VkDeviceSize size;
};

// List of suballocated staging buffers.
// Used to upload to/read from device local images.
struct wlr_vk_shared_buffer {
	struct wl_list link;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize buf_size;

	size_t allocs_size;
	size_t allocs_capacity;
	struct wlr_vk_allocation *allocs;
};

// Suballocated range on a buffer.
struct wlr_vk_buffer_span {
	struct wlr_vk_shared_buffer *buffer;
	struct wlr_vk_allocation alloc;
};

// Vulkan wlr_renderer implementation on top of wlr_vulkan.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;
	struct wlr_vulkan *vulkan;

	VkCommandPool command_pool;
	VkRenderPass render_pass;
	VkSampler sampler;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkDescriptorPool descriptor_pool;
	VkPipeline tex_pipe;
	VkPipeline quad_pipe;
	VkPipeline ellipse_pipe;
	VkFence fence;

	struct wlr_vk_render_surface *current;
	VkRect2D scissor; // needed for clearing

	bool linear_shm_images;
	struct {
		VkCommandBuffer cb;
		VkSemaphore signal;

		bool recording;
		struct wl_list buffers;
		uint32_t frame;
	} stage;

	bool host_images;
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
	VkCommandBuffer cb; // the cb being currently recorded
};

// wlr_vk_render_surface using a VkSurfaceKHR and swapchain
struct wlr_vk_swapchain_render_surface {
	struct wlr_vk_render_surface vk_rs;
	VkSurfaceKHR surface;
	struct wlr_vk_swapchain swapchain;
	int current_id; // current or last rendered buffer id

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
	} buffers[3];

	// we have 3 different buffers:
	// - one to render to that is currently not read/presented (back)
	// - one on which rendering has finished and that was swapped
	//   and is formally the front buffer
	// - the buffer that was the front buffer the last time we
	//   swapped buffers (before front) but might still be read,
	//   so cannot yet be used for rendering
	struct wlr_vk_offscreen_buffer *back; // currently or soon rendered
	struct wlr_vk_offscreen_buffer *front; // rendering finished, presenting
	struct wlr_vk_offscreen_buffer *old_front; // old presented
};

// Initializes a wlr_vulkan state. This will require
// the given extensions and fail if they cannot be found.
// Will automatically try to find all extensions needed.
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
void vulkan_render_surface_end(struct wlr_vk_render_surface *rs,
		VkSemaphore *wait, VkSemaphore *signal);

// stage utility - for uploading/retrieving data
// Gets an command buffer in recording state which is guaranteed to be
// executed before the next frame. Call submit_upload_cb to trigger submission
// manually (costly).
VkCommandBuffer wlr_vk_record_stage_cb(struct wlr_vk_renderer *renderer);
bool wlr_vk_submit_stage_wait(struct wlr_vk_renderer *renderer);

// Suballocates a buffer span with the given size that can be mapped
// and used as staging buffer. The allocation is implicitly released when the
// stage cb has finished execution.
struct wlr_vk_buffer_span wlr_vk_get_stage_span(
	struct wlr_vk_renderer *renderer, VkDeviceSize size);

// util
size_t wlr_clamp(size_t val, size_t low, size_t high);
const char *vulkan_strerror(VkResult err);
void vulkan_change_layout(VkCommandBuffer cb, VkImage img,
	VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
	VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta);

#define wlr_vulkan_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

#endif

