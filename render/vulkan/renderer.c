#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <render/vulkan.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include <wlr/render/vulkan.h>
#include <render/vulkan.h>
// #include "wayland-drm-server-protocol.h"

#include <wlr/backend/x11.h>
#include <backend/x11.h>
#include <vulkan/vulkan_xcb.h>
#include <wlr/backend/wayland.h>
#include <backend/wayland.h>
#include <vulkan/vulkan_wayland.h>
#include <wlr/backend/drm.h>
#include <backend/drm/drm.h>
#include <backend/drm/renderer.h>
#include <wlr/backend/headless.h>

#include "shaders/texture.vert.h"
#include "shaders/texture.frag.h"

#define wlr_vulkan_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

static const struct wlr_renderer_impl renderer_impl;
static const struct wlr_render_surface_impl swapchain_render_surface_impl;
static const struct wlr_render_surface_impl drm_render_surface_impl;

static struct wlr_vk_renderer *vulkan_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_vk_renderer *)wlr_renderer;
}

// static struct wlr_vk_render_surface *vulkan_get_render_surface(
// 		struct wlr_render_surface *wlr_rs) {
// 	assert(wlr_rs->impl == &swapchain_render_surface_impl ||
// 		wlr_rs->impl == &drm_render_surface_impl);
// 	return (struct wlr_vk_render_surface *)wlr_rs;
// }

static struct wlr_vk_swapchain_render_surface *
vulkan_get_render_surface_swapchain(struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &swapchain_render_surface_impl);
	return (struct wlr_vk_swapchain_render_surface *)wlr_rs;
}

static struct wlr_vk_drm_render_surface *
vulkan_get_render_surface_drm(struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &drm_render_surface_impl);
	return (struct wlr_vk_drm_render_surface *)wlr_rs;
}

static bool vulkan_render_surface_is_swapchain(
		struct wlr_render_surface *wlr_rs) {
	return wlr_rs->impl == &swapchain_render_surface_impl;
}

static bool vulkan_render_surface_is_drm(
		struct wlr_render_surface *wlr_rs) {
	return wlr_rs->impl == &drm_render_surface_impl;
}

// util
static bool backend_extensions(struct wlr_backend *backend,
		int *count, const char*** exts) {
	if(wlr_backend_is_headless(backend) || wlr_backend_is_drm(backend)) {
		*count = 0;
		*exts = NULL;
	} else if(wlr_backend_is_x11(backend)) {
		static const char* req[] = { VK_KHR_XCB_SURFACE_EXTENSION_NAME };
		*exts = req;
		*count = sizeof(req) / sizeof(req[0]);
	} else if(wlr_backend_is_wl(backend)) {
		static const char* req[] = { VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME };
		*exts = req;
		*count = sizeof(req) / sizeof(req[0]);
	} else {
		return false;
	}

	return true;
}

// swapchain render surface
static bool swapchain_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	VkResult res;
	bool success = true;

	// present
	VkPresentInfoKHR present_info = {0};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &rs->swapchain.swapchain;
	present_info.pImageIndices = &rs->current_id;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &rs->present;

	// TODO: (optionally; if available) use VK_KHR_incremental_present
	// and use the given damage region
	res = vkQueuePresentKHR(renderer->vulkan->present_queue, &present_info);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkQueuePresentKHR", res);
		success = false;
		goto clean;
	}

	// :(
	// sadly this is required due to the current api/rendering model of wlr
	// ideally we could use gpu and cpu in parallel (_without_ the
	// implicit synchronization overhead and mess of opengl drivers)
	vkDeviceWaitIdle(renderer->vulkan->dev);

clean:
	renderer->current = NULL;
	rs->current_id = 0xFFFFFFFF;
	return success;
}

static void swapchain_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	rs->vk_render_surface.width = width;
	rs->vk_render_surface.height = height;
	if (!wlr_vk_swapchain_resize(&rs->swapchain, width, height)) {
		wlr_log(WLR_ERROR, "Failed to resize vulkan render surface");
	}
}

static void swapchain_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;

	wlr_vk_swapchain_finish(&rs->swapchain);
	if (rs->surface) {
		vkDestroySurfaceKHR(renderer->vulkan->instance, rs->surface, NULL);
	}

	if (rs->vk_render_surface.cb) {
		vkFreeCommandBuffers(renderer->vulkan->dev, renderer->command_pool,
			1u, &rs->vk_render_surface.cb);
	}

	free(rs);
}

static const struct wlr_render_surface_impl swapchain_render_surface_impl = {
	.destroy = swapchain_render_surface_destroy,
	.swap_buffers = swapchain_swap_buffers,
	.resize = swapchain_render_surface_resize,
};

// drm render surface
static bool drm_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_vk_drm_render_surface *rs =
		vulkan_get_render_surface_drm(wlr_rs);

	rs->drm_surface->front = rs->front->bo;
	rs->drm_surface->back = rs->back->bo;

	struct wlr_vk_drm_buffer* tmp = rs->front;
	rs->front = rs->back;
	rs->back = tmp;

	// see swapchain_swap_buffers for why we need this atm
	vkDeviceWaitIdle(rs->vk_render_surface.renderer->vulkan->dev);
	return true;
}

static void drm_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	// TODO: recreate all images and buffers
}

static void drm_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
	// TODO: destroy buffers
	struct wlr_vk_drm_render_surface *rs =
		vulkan_get_render_surface_drm(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	if (rs->vk_render_surface.cb) {
		vkFreeCommandBuffers(renderer->vulkan->dev, renderer->command_pool,
			1u, &rs->vk_render_surface.cb);
	}

	free(rs);
}

static const struct wlr_render_surface_impl drm_render_surface_impl = {
	.destroy = drm_render_surface_destroy,
	.swap_buffers = drm_swap_buffers,
	.resize = drm_render_surface_resize,
};

// renderer
static void vulkan_begin_rp(struct wlr_vk_render_surface *rs,
		VkCommandBuffer cb, VkFramebuffer fb) {
	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(cb, &begin_info);

	uint32_t width = rs->width;
	uint32_t height = rs->height;
	VkRect2D rect = {{0, 0}, {width, height}};
	VkClearValue clear_value = {0};

	VkRenderPassBeginInfo rp_info = {0};
	rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_info.renderArea = rect;
	rp_info.renderPass = rs->renderer->render_pass;
	rp_info.framebuffer = fb;
	rp_info.clearValueCount = 1;
	rp_info.pClearValues = &clear_value;
	vkCmdBeginRenderPass(cb, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0.f, 0.f, (float) width, (float) height, 0.f, 1.f};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &rect);
}

static bool vulkan_begin_drm(struct wlr_vk_drm_render_surface *rs,
		int *buffer_age) {
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	if (buffer_age) {
		*buffer_age = 2;
	}

	vulkan_begin_rp(&rs->vk_render_surface, rs->vk_render_surface.cb,
		rs->back->buffer.framebuffer);
	renderer->current = &rs->vk_render_surface;
	return true;
}

static bool vulkan_begin_swapchain(struct wlr_vk_swapchain_render_surface *rs,
		int *buffer_age) {
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	struct wlr_vulkan *vulkan = renderer->vulkan;

	// TODO: better handling (skipping) out of date/suboptimal possible?
	// the problem is that we can't really do anything ourselves in
	// this case (the render surface has to be resized).
	// Returning false makes sense but that requires that all callers
	// interpret it correctly: not a critical error, just not possible
	// to render at the moment.
	// We could ignore the suboptimal return value though and simply
	// render anyways (might get a scaled frame)
	VkResult res;
	uint32_t id;
	res = vkAcquireNextImageKHR(vulkan->dev, rs->swapchain.swapchain,
		0xFFFFFFFF, rs->acquire, VK_NULL_HANDLE, &id);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAcquireNextImageKHR", res);
		return false;
	}

	// track buffer age
	assert(id < rs->swapchain.image_count);
	for(unsigned i = 0; i < rs->swapchain.image_count; ++i) {
		if(i == id) {
			if (buffer_age) {
				// TODO: we currently don't support it correctly
				// see render pass creation, we'd need load_op load
				// and a manual image layout transition...
				// *buffer_age = rs->swapchain.buffers[i].age;
				*buffer_age = -1;
			}
			rs->swapchain.buffers[i].age = 1;
		} else {
			++rs->swapchain.buffers[i].age;
		}
	}

	// start recording
	VkFramebuffer fb = rs->swapchain.buffers[id].framebuffer;
	vulkan_begin_rp(&rs->vk_render_surface, rs->vk_render_surface.cb, fb);

	renderer->current = &rs->vk_render_surface;
	rs->current_id = id;
	return true;
}

static bool vulkan_begin(struct wlr_renderer *wlr_renderer,
		struct wlr_render_surface *wlr_rs, int *buffer_age) {
	if (vulkan_render_surface_is_swapchain(wlr_rs)) {
		return vulkan_begin_swapchain(
			vulkan_get_render_surface_swapchain(wlr_rs), buffer_age);
	} else if (vulkan_render_surface_is_drm(wlr_rs)) {
		return vulkan_begin_drm(
			vulkan_get_render_surface_drm(wlr_rs), buffer_age);
	}

	wlr_log(WLR_ERROR, "Invalid render surface");
	return false;
}

static void vulkan_end_drm(struct wlr_vk_drm_render_surface *rs) {
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	VkCommandBuffer cb = rs->vk_render_surface.cb;
	vkCmdEndRenderPass(cb);
	vkEndCommandBuffer(cb);

	// submit
	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pCommandBuffers = &cb;
	submit_info.commandBufferCount = 1;

	VkResult res = vkQueueSubmit(renderer->vulkan->graphics_queue, 1,
		&submit_info, VK_NULL_HANDLE);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkQueueSubmit", res);
	}
}

static void vulkan_end_swapchain(struct wlr_vk_swapchain_render_surface *rs) {
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	VkCommandBuffer cb = rs->vk_render_surface.cb;
	VkResult res;

	vkCmdEndRenderPass(cb);
	vkEndCommandBuffer(cb);

	// submit
	VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pWaitSemaphores = &rs->acquire;
	submit_info.pWaitDstStageMask = &stage;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pCommandBuffers = &cb;
	submit_info.commandBufferCount = 1;
	submit_info.pSignalSemaphores = &rs->present;
	submit_info.signalSemaphoreCount = 1;

	res = vkQueueSubmit(renderer->vulkan->graphics_queue, 1, &submit_info,
		VK_NULL_HANDLE);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkQueueSubmit", res);
	}
}

static void vulkan_end(struct wlr_renderer *wlr_renderer) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);

	if (vulkan_render_surface_is_swapchain(&renderer->current->render_surface)) {
		vulkan_end_swapchain(vulkan_get_render_surface_swapchain(
			&renderer->current->render_surface));
	} else if (vulkan_render_surface_is_drm(&renderer->current->render_surface)) {
		vulkan_end_drm(vulkan_get_render_surface_drm(
			&renderer->current->render_surface));
	} else {
		wlr_log(WLR_ERROR, "Invalid render surface");
	}
}

static bool vulkan_render_texture_with_matrix(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *texture, const float matrix[9], float alpha) {
	// TODO
	// bind pipeline (if not already bound)
	// write matrix into ubo, update descriptor set for texture
	//  even better: every texture has its own descriptor set (with bound image)
	// render shit

	return false;
}

static void vulkan_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	VkClearAttachment att = {0};
	att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	att.colorAttachment = 0u;
	memcpy(&att.clearValue.color.float32, color, 4 * sizeof(float));

	VkClearRect rect = {0};
	rect.rect.extent.width = renderer->current->width;
	rect.rect.extent.height = renderer->current->height;
	rect.layerCount = 1;
	vkCmdClearAttachments(cb, 1, &att, 1, &rect);
}

static void vulkan_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	VkRect2D rect = {{box->x, box->y}, {box->width, box->height}};
	vkCmdSetScissor(cb, 0, 1, &rect);
}

static bool vulkan_resource_is_wl_drm_buffer(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource) {
	return false;
}

const enum wl_shm_format *vulkan_formats(struct wlr_renderer *wlr_renderer,
		size_t *len) {
	return get_vulkan_formats(len);
}

static void vulkan_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	// TODO
}

static void vulkan_render_ellipse_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	// TODO
}

static void vulkan_wl_drm_buffer_get_size(struct wlr_renderer *wlr_renderer,
		struct wl_resource *buffer, int *width, int *height) {
	*width = 0;
	*height = 0;
}

static int vulkan_get_dmabuf_formats(struct wlr_renderer *wlr_renderer,
		int **formats) {
	return 0;
}

static int vulkan_get_dmabuf_modifiers(struct wlr_renderer *wlr_renderer,
		int format, uint64_t **modifiers) {
	return 0;
}

static bool vulkan_read_pixels(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt, uint32_t *flags, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	return false;
}

static bool vulkan_format_supported(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt) {
	return get_vulkan_format_from_wl(wl_fmt) != NULL;
}

static struct wlr_texture *vulkan_texture_from_pixels(
		struct wlr_renderer *wlr_renderer, enum wl_shm_format wl_fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	return wlr_vk_texture_from_pixels(renderer->vulkan, wl_fmt, stride,
		width, height, data);
}

static struct wlr_texture *vulkan_texture_from_wl_drm(
		struct wlr_renderer *wlr_renderer, struct wl_resource *data) {
	return NULL;
}

static struct wlr_texture *vulkan_texture_from_dmabuf(
		struct wlr_renderer *wlr_renderer,
		struct wlr_dmabuf_attributes *attribs) {
	return NULL;
}

static void vulkan_destroy(struct wlr_renderer *wlr_renderer) {
	// TODO
}

static void vulkan_init_wl_display(struct wlr_renderer* wlr_renderer,
		struct wl_display* wl_display) {
}

static struct wlr_render_surface *vulkan_create_drm_render_surface(
		struct wlr_vk_renderer *renderer, void *handle,
		uint32_t width, uint32_t height) {
	VkResult res;
	struct wlr_vulkan *vulkan = renderer->vulkan;
	struct wlr_vk_drm_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->vk_render_surface.renderer = renderer;
	rs->vk_render_surface.width = width;
	rs->vk_render_surface.height = height;
	rs->drm_surface = (struct wlr_drm_surface *)handle;
	wlr_render_surface_init(&rs->vk_render_surface.render_surface,
		&drm_render_surface_impl);

	// command buffer
	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = 1u;
	res = vkAllocateCommandBuffers(vulkan->dev, &cmd_buf_info,
		&rs->vk_render_surface.cb);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		return false;
	}

	// initialize buffers
	for(unsigned i = 0u; i < 2; ++i) {
		struct wlr_vk_drm_buffer *buf = &rs->buffers[i];

		// we currently need gbm_bo_use_linear to import it into
		// vulkan with linear layout. Could probably be solved
		// with the (future) vulkan modifiers extension
		buf->bo = gbm_bo_create(rs->drm_surface->renderer->gbm,
            width, height, GBM_FORMAT_ARGB8888,
			rs->drm_surface->flags | GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
		if (!buf->bo) {
			wlr_log(WLR_ERROR, "Failed to create gbm_bo");
			goto error;
		}

		int fd = gbm_bo_get_fd(buf->bo);
		if (fd < 0) {
			wlr_log(WLR_ERROR, "Failed to retrieve gbm_bo fd");
			goto error;
		}

		// TODO: use queried external properties
		// TODO: we probably want unorm format here (adapt render pass)
		// swapped bit order compared to gbm (we used argb)
		const VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
		const VkExternalMemoryHandleTypeFlagBits htype =
			VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

		VkExternalMemoryImageCreateInfo extimg_info = {0};
		extimg_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
		extimg_info.handleTypes = htype;

		VkImageCreateInfo img_info = {0};
		img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		img_info.pNext = &extimg_info;
		img_info.imageType = VK_IMAGE_TYPE_2D;
		img_info.format = format;
		img_info.mipLevels = 1;
		img_info.arrayLayers = 1;
		img_info.samples = VK_SAMPLE_COUNT_1_BIT;
		img_info.tiling = VK_IMAGE_TILING_LINEAR;
		img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		img_info.extent = (VkExtent3D) { width, height, 1 };
		img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		res = vkCreateImage(vulkan->dev, &img_info, NULL, &buf->buffer.image);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateImage (import)", res);
			goto error;
		}

		// allocate memory
		// we always use dedicated memory anyways
		VkImageMemoryRequirementsInfo2 mem_reqs_info = {0};
		mem_reqs_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
		mem_reqs_info.image = buf->buffer.image;

		VkMemoryRequirements2 mem_reqs = {0};
		mem_reqs.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		vkGetImageMemoryRequirements2(vulkan->dev, &mem_reqs_info, &mem_reqs);

		// create memory
		struct VkMemoryFdPropertiesKHR props = {0};
		props.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
		vulkan->api.getMemoryFdPropertiesKHR(renderer->vulkan->dev,
			htype, fd, &props);

		VkImportMemoryFdInfoKHR import_info = {0};
		import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
		import_info.fd = fd;
		import_info.handleType = htype;

		// query fd size, as suggested by vulkan api spec doc
		// off_t buf_size = lseek(fd, 0L, SEEK_END);
		// if (buf_size == (off_t) -1) {
		// 	wlr_log_errno(WLR_ERROR, "lseek");
		// 	goto error;
		// }
		// lseek(fd, 0L, SEEK_SET);

		VkMemoryAllocateInfo mem_info = {0};
		mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_info.pNext = &import_info;
		// mem_info.allocationSize = buf_size;
		mem_info.allocationSize = mem_reqs.memoryRequirements.size;
		mem_info.memoryTypeIndex = wlr_vulkan_find_mem_type(vulkan,
			0, props.memoryTypeBits);

		res = vkAllocateMemory(vulkan->dev, &mem_info,
			NULL, &buf->memory);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkAllocateMemory (import)", res);
			goto error;
		}

		// bind it
		res = vkBindImageMemory(vulkan->dev, buf->buffer.image,
			buf->memory, 0);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkBindMemory (imported)", res);
			goto error;
		}

		VkImageViewCreateInfo view_info = {0};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.pNext = NULL;
		view_info.format = format;
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
		view_info.image = buf->buffer.image;

		res = vkCreateImageView(vulkan->dev, &view_info, NULL,
			&buf->buffer.image_view);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateImageView", res);
			goto error;
		}

		VkFramebufferCreateInfo fb_info = {0};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments = &buf->buffer.image_view;
		fb_info.renderPass = renderer->render_pass;
		fb_info.width = width;
		fb_info.height = height;
		fb_info.layers = 1;

		res = vkCreateFramebuffer(vulkan->dev, &fb_info, NULL,
			&buf->buffer.framebuffer);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateFramebuffer", res);
			goto error;
		}
	}

	rs->front = &rs->buffers[0];
	rs->back = &rs->buffers[1];

	return &rs->vk_render_surface.render_surface;

error:
	drm_render_surface_destroy(&rs->vk_render_surface.render_surface);
	return NULL;
}

static struct wlr_render_surface *vulkan_create_render_surface(
		struct wlr_renderer *wlr_renderer, void *handle,
		uint32_t width, uint32_t height) {

	VkResult res;
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	if (wlr_backend_is_drm(renderer->backend)) {
		return vulkan_create_drm_render_surface(renderer, handle,
			width, height);
	}

	struct wlr_vk_swapchain_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->vk_render_surface.renderer = renderer;
	rs->vk_render_surface.width = width;
	rs->vk_render_surface.height = height;
	VkSemaphoreCreateInfo sem_info = {0};
	sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	res = vkCreateSemaphore(renderer->vulkan->dev, &sem_info, NULL,
		&rs->acquire);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateSemaphore", res);
		goto error;
	}

	res = vkCreateSemaphore(renderer->vulkan->dev, &sem_info, NULL,
		&rs->present);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateSemaphore", res);
		goto error;
	}

	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = 1u;
	res = vkAllocateCommandBuffers(renderer->vulkan->dev, &cmd_buf_info,
		&rs->vk_render_surface.cb);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		return false;
	}

	wlr_render_surface_init(&rs->vk_render_surface.render_surface,
		&swapchain_render_surface_impl);
	if (wlr_backend_is_x11(renderer->backend)) {
		struct wlr_x11_backend *x11 =
			(struct wlr_x11_backend *)renderer->backend;
		VkXcbSurfaceCreateInfoKHR info = {0};
		info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
		info.connection = x11->xcb_conn;
		info.window = *(xcb_window_t*) handle;
		VkResult res = vkCreateXcbSurfaceKHR(renderer->vulkan->instance, &info,
			NULL, &rs->surface);
		if(res != VK_SUCCESS) {
			wlr_vulkan_error("Failed to create x11 vk surface", res);
			goto error;
		}

		wlr_vk_swapchain_init(&rs->swapchain, renderer, rs->surface,
			width, height, true);
		return &rs->vk_render_surface.render_surface;
	} else if (wlr_backend_is_wl(renderer->backend)) {
		struct wlr_wl_backend *wl =
			(struct wlr_wl_backend *)renderer->backend;
		VkWaylandSurfaceCreateInfoKHR info = {0};
		info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
		info.display = wl->remote_display;
		info.surface = (struct wl_surface *) handle;
		VkResult res = vkCreateWaylandSurfaceKHR(renderer->vulkan->instance,
			&info, NULL, &rs->surface);
		if(res != VK_SUCCESS) {
			wlr_vulkan_error("Failed to create wl vk surface", res);
			goto error;
		}

		wlr_vk_swapchain_init(&rs->swapchain, renderer, rs->surface,
			width, height, true);
		return &rs->vk_render_surface.render_surface;
	}

	free(rs);
	return NULL;

error:
	wlr_render_surface_destroy(&rs->vk_render_surface.render_surface);
	return NULL;
}

static const struct wlr_renderer_impl renderer_impl = {
	.begin = vulkan_begin,
	.end = vulkan_end,
	.clear = vulkan_clear,
	.scissor = vulkan_scissor,
	.render_texture_with_matrix = vulkan_render_texture_with_matrix,
	.render_quad_with_matrix = vulkan_render_quad_with_matrix,
	.render_ellipse_with_matrix = vulkan_render_ellipse_with_matrix,
	.formats = vulkan_formats,
	.resource_is_wl_drm_buffer = vulkan_resource_is_wl_drm_buffer,
	.wl_drm_buffer_get_size = vulkan_wl_drm_buffer_get_size,
	.get_dmabuf_formats = vulkan_get_dmabuf_formats,
	.get_dmabuf_modifiers = vulkan_get_dmabuf_modifiers,
	.read_pixels = vulkan_read_pixels,
	.format_supported = vulkan_format_supported,
	.texture_from_pixels = vulkan_texture_from_pixels,
	.texture_from_wl_drm = vulkan_texture_from_wl_drm,
	.texture_from_dmabuf = vulkan_texture_from_dmabuf,
	.destroy = vulkan_destroy,
	.init_wl_display = vulkan_init_wl_display,
	.create_render_surface = vulkan_create_render_surface,
};

static bool init_pipeline(struct wlr_vk_renderer *renderer) {
	// util
	VkDevice dev = renderer->vulkan->dev;
	VkResult res;

	// TODO: correct formats (from swapchain), loading
	// renderpass
	VkAttachmentDescription attachment = {0};
	attachment.format = VK_FORMAT_B8G8R8A8_SRGB;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	// attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	// attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_ref = {0};
	color_ref.attachment = 0u;
	color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {0};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;

	VkSubpassDependency deps[2] = {0};
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_HOST_BIT |
		VK_PIPELINE_STAGE_TRANSFER_BIT |
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[0].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[0].dstSubpass = 0;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	deps[0].dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT |
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
		VK_ACCESS_SHADER_READ_BIT;

	deps[1].srcSubpass = 1;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
	deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT |
		VK_ACCESS_MEMORY_READ_BIT;

	VkRenderPassCreateInfo rp_info = {0};
	rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp_info.attachmentCount = 1;
	rp_info.pAttachments = &attachment;
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = &subpass;
	rp_info.dependencyCount = 2u;
	rp_info.pDependencies = deps;

	res = vkCreateRenderPass(dev, &rp_info, NULL, &renderer->render_pass);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create render pass", res);
		goto error;
	}

	// sampler
	VkSamplerCreateInfo sampler_info = {0};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler_info.maxAnisotropy = 1.f;
	sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
	sampler_info.minLod = 0.f;
	sampler_info.maxLod = 0.25f;
	sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

	res = vkCreateSampler(dev, &sampler_info, NULL, &renderer->sampler);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create sampler", res);
		goto error;
	}

	// layouts
	// descriptor set
	VkDescriptorSetLayoutBinding ds_bindings[2] = {{
		0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
		VK_SHADER_STAGE_VERTEX_BIT, NULL,
	}, {
		1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		VK_SHADER_STAGE_FRAGMENT_BIT, &renderer->sampler
	}};

	VkDescriptorSetLayoutCreateInfo ds_info = {0};
	ds_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	ds_info.bindingCount = 2;
	ds_info.pBindings = ds_bindings;

	res = vkCreateDescriptorSetLayout(dev, &ds_info, NULL,
		&renderer->descriptor_set_layout);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "Failed to create descriptor set layout: %d", res);
		goto error;
	}

	// pipeline layout
	VkPipelineLayoutCreateInfo pl_info = {0};
	pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_info.setLayoutCount = 1;
	pl_info.pSetLayouts = &renderer->descriptor_set_layout;

	res = vkCreatePipelineLayout(dev, &pl_info, NULL, &renderer->pipeline_layout);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create pipeline layout", res);
		goto error;
	}

	// pipeline shader frag
	VkShaderModuleCreateInfo frag_info = {0};
	frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	frag_info.codeSize = sizeof(texture_frag_data);
	frag_info.pCode = texture_frag_data;

	VkShaderModule frag_module;
	res = vkCreateShaderModule(dev, &frag_info, NULL, &frag_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create fragment shader module", res);
		goto error;
	}

	// vert
	VkShaderModuleCreateInfo vert_info = {0};
	vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vert_info.codeSize = sizeof(texture_vert_data);
	vert_info.pCode = texture_vert_data;

	VkShaderModule vert_module;
	res = vkCreateShaderModule(dev, &vert_info, NULL, &vert_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create vertex shader module", res);
		goto error;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_module, "main", NULL
	}, {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag_module, "main", NULL
	}};

	// info
	VkPipelineInputAssemblyStateCreateInfo assembly = {0};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

	VkPipelineRasterizationStateCreateInfo rasterization = {0};
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.f;

	VkPipelineColorBlendAttachmentState blend_attachment = {0};
	blend_attachment.blendEnable = true;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend = {0};
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;

	VkPipelineMultisampleStateCreateInfo multisample = {0};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineViewportStateCreateInfo viewport = {0};
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	VkDynamicState dynStates[2] = {
		VK_DYNAMIC_STATE_VIEWPORT,
    	VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamic = {0};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.pDynamicStates = dynStates;
	dynamic.dynamicStateCount = 2;

	VkPipelineVertexInputStateCreateInfo vertex = {0};
	vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkGraphicsPipelineCreateInfo pinfo = {0};
	pinfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pinfo.layout = renderer->pipeline_layout;
	pinfo.renderPass = renderer->render_pass;
	pinfo.subpass = 0;
	pinfo.stageCount = 2;
	pinfo.pStages = stages;

	pinfo.pInputAssemblyState = &assembly;
	pinfo.pRasterizationState = &rasterization;
	pinfo.pColorBlendState = &blend;
	pinfo.pMultisampleState = &multisample;
	pinfo.pViewportState = &viewport;
	pinfo.pDynamicState = &dynamic;
	pinfo.pVertexInputState = &vertex;

	// NOTE: use could use a cache here for faster loading
	// store it somewhere like $XDG_CACHE_HOME/wlroots/vk_pipe_cache
	VkPipelineCache cache = VK_NULL_HANDLE;
	res = vkCreateGraphicsPipelines(dev, cache, 1, &pinfo, NULL,
		&renderer->pipeline);
	if(res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "failed to create vulkan pipelines: %d", res);
		goto error;
	}

	return true;

error:
	// TODO: cleanup
	return false;
}

struct wlr_renderer *wlr_vk_renderer_create(struct wlr_backend *backend) {
	bool debug = true;
	int ini_ext_count;
	const char **ini_exts;

	// TODO: when getting a drm backend, we should use the same gpu as
	// the backend. Not sure if there is a reliable (or even mesa)
	// way to detect if a VkPhysicalDevice is the same as a gbm_device
	// (or drm fd) though.
	if (!backend_extensions(backend, &ini_ext_count, &ini_exts)) {
		wlr_log(WLR_ERROR, "Unsupported backend in vulkan renderer");
		return NULL;
	}

	struct wlr_vulkan *vulkan = wlr_vulkan_create(
		ini_ext_count, ini_exts, 0, NULL, debug);
	if (!vulkan) {
		wlr_log(WLR_ERROR, "Failed to initialize vulkan");
		return NULL;
	}

	struct wlr_vk_renderer *renderer;
	VkResult res;
	if (!(renderer = calloc(1, sizeof(*renderer)))) {
		wlr_log_errno(WLR_ERROR, "failed to allocate wlr_vk_renderer");
		return NULL;
	}

	renderer->backend = backend;
	renderer->vulkan = vulkan;
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

	VkCommandPoolCreateInfo pool_info = {0};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan->graphics_queue_fam;
	res = vkCreateCommandPool(vulkan->dev, &pool_info, NULL,
		&renderer->command_pool);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateCommandPool", res);
		goto error;
	}

	if (!init_pipeline(renderer)) {
		wlr_log(WLR_ERROR, "Could not init vulkan pipeline");
		goto error;
	}

	return &renderer->wlr_renderer;

error:
	vulkan_destroy(&renderer->wlr_renderer);
	return NULL;
}
