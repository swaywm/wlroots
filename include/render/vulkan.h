#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/interface.h>

struct wlr_vk_descriptor_pool;

// Central vulkan state that should only be needed once per compositor.
// Ownership shared by renderers, object will be destroyed when no more
// devices are in the list.
struct wlr_vk_instance {
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;

	// will try to enable all extensions that might be used by any wlr
	// parts later on
	unsigned extension_count;
	const char **extensions;

	struct {
		PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT;
		PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT;
	} api;

	struct wl_list devices;
};

// Creates and initializes a vulkan instance.
// Will try to enable the given extensions but not fail if they are not
// available which can later be checked by the caller.
// The debug parameter determines if validation layers are enabled and a
// debug messenger created.
// `compositor_name` and `compositor_version` are passed to the vulkan driver.
struct wlr_vk_instance *wlr_vk_instance_create(
	unsigned ext_count, const char **exts, bool debug,
	const char *compositor_name, unsigned compositor_version);
void wlr_vk_instance_destroy(struct wlr_vk_instance *ini);


struct wlr_vk_queue {
	uint32_t family;
	VkQueue queue;
};

// Logical vulkan device state.
// Ownership can be shared by multiple renderers, reference counted
// with `renderers`.
struct wlr_vk_device {
	struct wlr_vk_instance *instance;
	struct wl_list link;
	uint32_t renderers;

	VkPhysicalDevice phdev;
	VkDevice dev;

	unsigned extension_count;
	const char **extensions;

	unsigned queue_count;
	struct wlr_vk_queue *queues;

	struct {
		PFN_vkGetMemoryFdPropertiesKHR getMemoryFdPropertiesKHR;
	} api;
};

// Creates a device for the given instance and physical devices (which must
// be part of a single device group).
// Will try to enable the given extensions but not fail if they are not
// available which can later be checked by the caller.
// `queues` must not contain the same family multiple times and will
// contain the retrieved queues.
struct wlr_vk_device *wlr_vk_device_create(struct wlr_vk_instance *ini,
	VkPhysicalDevice phdev, unsigned ext_count, const char **exts,
	unsigned queue_count, struct wlr_vk_queue *queues);
void wlr_vk_device_destroy(struct wlr_vk_device *dev);

// Tries to find any memory bit for the given vulkan device that
// supports the given flags and is set in req_bits (e.g. if memory
// type 2 is ok, (req_bits & (1 << 2)) must not be 0.
// Set req_bits to 0xFFFFFFFF to allow all types.
int wlr_vk_find_mem_type(struct wlr_vk_device *device,
	VkMemoryPropertyFlags flags, uint32_t req_bits);


struct wlr_vk_pixel_format {
	uint32_t wl_format; // except {a,x}rgb also the drm format
	VkFormat vk_format;
	int bpp;
	bool has_alpha;
};

// Returns all known format mappings.
// Might not be supported for gpu/usecase.
const struct wlr_vk_pixel_format *vulkan_get_format_list(size_t *len);
const struct wlr_vk_pixel_format *vulkan_get_format_from_wl(
	enum wl_shm_format fmt);

struct wlr_vk_pixel_format_props {
	struct wlr_vk_pixel_format format;
	VkExtent2D max_extent;
	bool as_dmabuf; // whether it's compatible with dmabuf
	VkExternalMemoryFeatureFlagBits dma_features;
};

// Returns whether the format given in `props->format` can be used on the given
// device with the given usage/tiling. If supported,
// will fill out the passed properties.
bool wlr_vk_query_format(struct wlr_vk_device *dev,
	struct wlr_vk_pixel_format_props *format, VkImageUsageFlags usage,
	VkImageTiling tiling);


// Vulkan wlr_renderer implementation on top of a wlr_vk_device.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;
	struct wlr_vk_device *dev;

	VkCommandPool command_pool;
	VkRenderPass render_pass;
	VkSampler sampler;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline tex_pipe;
	VkPipeline quad_pipe;
	VkPipeline ellipse_pipe;
	VkFence fence;
	VkFormat format; // used in renderpass

	struct wlr_vk_queue graphics_queue;
	struct wlr_vk_queue present_queue;

	struct wlr_vk_render_surface *current;
	VkRect2D scissor; // needed for clearing

	uint32_t format_count; // number of supported formats below
	enum wl_shm_format *wl_formats;
	struct wlr_vk_pixel_format_props *formats;

	size_t last_pool_size;
	struct wl_list descriptor_pools; // type wlr_vk_descriptor_pool

	struct {
		VkCommandBuffer cb;
		VkSemaphore signal;

		bool recording;
		struct wl_list buffers; // type wlr_vk_shared_buffer
		uint32_t frame;
	} stage;

	bool host_images; // whether to allocate images on host memory
};

// Creates a vulkan renderer for the given device.
struct wlr_renderer *wlr_vk_renderer_create_for_device(
	struct wlr_vk_device *dev, const struct wlr_vk_queue *graphics,
	const struct wlr_vk_queue *present);

// stage utility - for uploading/retrieving data
// Gets an command buffer in recording state which is guaranteed to be
// executed before the next frame. Call submit_upload_cb to trigger submission
// manually (costly).
VkCommandBuffer wlr_vk_record_stage_cb(struct wlr_vk_renderer *renderer);

// Submits the current stage command buffer and waits until it has
// finished execution.
bool wlr_vk_submit_stage_wait(struct wlr_vk_renderer *renderer);

// Suballocates a buffer span with the given size that can be mapped
// and used as staging buffer. The allocation is implicitly released when the
// stage cb has finished execution.
struct wlr_vk_buffer_span wlr_vk_get_stage_span(
	struct wlr_vk_renderer *renderer, VkDeviceSize size);

// Tries to allocate a texture descriptor set. Will additionally
// return the pool it was allocated from when successful (for freeing it later).
struct wlr_vk_descriptor_pool *wlr_vk_alloc_texture_ds(
	struct wlr_vk_renderer *renderer, VkDescriptorSet *ds);

// Frees the given descriptor set from the pool its pool.
void wlr_vk_free_ds(struct wlr_vk_renderer *renderer,
	struct wlr_vk_descriptor_pool *pool, VkDescriptorSet ds);
struct wlr_vk_pixel_format_props *wlr_vk_format_from_wl(
	struct wlr_vk_renderer *renderer, enum wl_shm_format format);
struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *r);

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
	struct wlr_vk_descriptor_pool *ds_pool;
	uint32_t last_written;
};

struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);

struct wlr_vk_descriptor_pool {
	VkDescriptorPool pool;
	uint32_t free;
	struct wl_list link;
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


struct wlr_vk_render_surface {
	struct wlr_render_surface rs;
	struct wlr_vk_renderer *renderer;
	VkCommandBuffer cb; // the cb being currently recorded
};

// One buffer of a swapchain (VkSwapchainKHR or custom).
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
struct wlr_vk_render_surface *vulkan_get_render_surface(
	struct wlr_render_surface *wlr_rs);


// util
bool vulkan_has_extension(unsigned count, const char **exts, const char *find);
const char *vulkan_strerror(VkResult err);
void vulkan_change_layout(VkCommandBuffer cb, VkImage img,
	VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
	VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta);

#define wlr_vk_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

#endif

