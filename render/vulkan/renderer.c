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
// #include "wayland-offscreen-server-protocol.h"

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
static const struct wlr_render_surface_impl offscreen_render_surface_impl;

struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_vk_renderer *)wlr_renderer;
}

// static struct wlr_vk_render_surface *vulkan_get_render_surface(
// 		struct wlr_render_surface *wlr_rs) {
// 	assert(wlr_rs->impl == &swapchain_render_surface_impl ||
// 		wlr_rs->impl == &offscreen_render_surface_impl);
// 	return (struct wlr_vk_render_surface *)wlr_rs;
// }

static struct wlr_vk_swapchain_render_surface *
vulkan_get_render_surface_swapchain(struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &swapchain_render_surface_impl);
	return (struct wlr_vk_swapchain_render_surface *)wlr_rs;
}

static struct wlr_vk_offscreen_render_surface *
vulkan_get_render_surface_offscreen(struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &offscreen_render_surface_impl);
	return (struct wlr_vk_offscreen_render_surface *)wlr_rs;
}

static bool vulkan_render_surface_is_swapchain(
		struct wlr_render_surface *wlr_rs) {
	return wlr_rs->impl == &swapchain_render_surface_impl;
}

static bool vulkan_render_surface_is_offscreen(
		struct wlr_render_surface *wlr_rs) {
	return wlr_rs->impl == &offscreen_render_surface_impl;
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

static int swapchain_render_surface_buffer_age(struct wlr_render_surface *wlr_rs) {
	// TODO: in vulkan we can't know the next buffer that will be
	// used by the swapchain. The swapchain design simply doesn't
	// allow in _any_ way to completely skip frames based on buffer
	// age. But we could at least use a correct damage region when
	// wlr_renderer_begin would again return a buffer_age value (for
	// cases like this: where we only can determine the buffer age
	// when starting to render)
	//
	// struct wlr_vk_swapchain_render_surface *rs =
	// 	vulkan_get_render_surface_swapchain(wlr_rs);
	// *buffer_age = rs->swapchain.buffers[???].age;
	return -1;
}

static const struct wlr_render_surface_impl swapchain_render_surface_impl = {
	.buffer_age = swapchain_render_surface_buffer_age,
	.destroy = swapchain_render_surface_destroy,
	.swap_buffers = swapchain_swap_buffers,
	.resize = swapchain_render_surface_resize,
};

// offscreen render surface
static bool offscreen_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);

	struct wlr_vk_offscreen_buffer* tmp = rs->front;
	rs->front = rs->back;
	rs->front->age = 2;
	rs->back = tmp;
	rs->drm_surface->front = rs->front->bo;

	// see swapchain_swap_buffers for why we need this atm
	vkDeviceWaitIdle(rs->vk_render_surface.renderer->vulkan->dev);
	return true;
}

// TODO
// static void offscreen_render_surface_destroy_buffer(
// 		struct wlr_vk_offscreen_render_surface *wlr_rs) {
// 	struct wlr_vk_offscreen_render_surface *rs =
// 		vulkan_get_render_surface_offscreen(wlr_rs);
//
// }

static void offscreen_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	// TODO: recreate all images and buffers
}

static void offscreen_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
	// TODO: destroy buffers
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;
	if (rs->vk_render_surface.cb) {
		vkFreeCommandBuffers(renderer->vulkan->dev, renderer->command_pool,
			1u, &rs->vk_render_surface.cb);
	}

	free(rs);
}

static int offscreen_render_surface_buffer_age(struct wlr_render_surface *wlr_rs) {
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);

	// is always 2 once the buffer was used since we have only 2 buffers
	// we always swap
	return rs->back->age;
}

static const struct wlr_render_surface_impl offscreen_render_surface_impl = {
	.buffer_age = offscreen_render_surface_buffer_age,
	.destroy = offscreen_render_surface_destroy,
	.swap_buffers = offscreen_swap_buffers,
	.resize = offscreen_render_surface_resize,
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

static bool vulkan_begin_offscreen(struct wlr_vk_offscreen_render_surface *rs) {
	struct wlr_vk_renderer *renderer = rs->vk_render_surface.renderer;

	vulkan_begin_rp(&rs->vk_render_surface, rs->vk_render_surface.cb,
		rs->back->buffer.framebuffer);
	renderer->current = &rs->vk_render_surface;
	return true;
}

static bool vulkan_begin_swapchain(struct wlr_vk_swapchain_render_surface *rs) {
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
	// NOTE: currently never used, see comment in
	// swapchain_render_surface_buffer_age
	assert(id < rs->swapchain.image_count);
	for(unsigned i = 0; i < rs->swapchain.image_count; ++i) {
		if(i == id) {
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
		struct wlr_render_surface *wlr_rs) {
	if (vulkan_render_surface_is_swapchain(wlr_rs)) {
		return vulkan_begin_swapchain(
			vulkan_get_render_surface_swapchain(wlr_rs));
	} else if (vulkan_render_surface_is_offscreen(wlr_rs)) {
		return vulkan_begin_offscreen(vulkan_get_render_surface_offscreen(wlr_rs));
	}

	wlr_log(WLR_ERROR, "Invalid render surface");
	return false;
}

static void vulkan_end_offscreen(struct wlr_vk_offscreen_render_surface *rs) {
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
	} else if (vulkan_render_surface_is_offscreen(&renderer->current->render_surface)) {
		vulkan_end_offscreen(vulkan_get_render_surface_offscreen(
			&renderer->current->render_surface));
	} else {
		wlr_log(WLR_ERROR, "Invalid render surface");
	}

	renderer->current = NULL;
}

static bool vulkan_render_texture_with_matrix(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *wlr_texture, const float matrix[9], float alpha) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);

	// TODO: alpha
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->pipeline);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->pipeline_layout, 0, 1, &texture->ds, 0, NULL);

	float mat4[4][4] = {0};
	mat4[0][0] = matrix[0];
	mat4[0][1] = matrix[1];
	mat4[0][3] = matrix[2];

	mat4[1][0] = matrix[3];
	mat4[1][1] = matrix[4];
	mat4[1][3] = matrix[5];

	mat4[2][2] = 1.f;
	mat4[3][3] = 1.f;

	wlr_log(WLR_DEBUG, "%f %f\n", mat4[0][0], mat4[1][1]);

	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mat4);
	vkCmdDraw(cb, 4, 1, 0, 0);

	return true;
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

	VkRect2D rect = {{0, 0}, {renderer->current->width, renderer->current->height}};
	if (box) {
		rect = (VkRect2D) {{box->x, box->y}, {box->width, box->height}};
	}

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
	return wlr_vk_texture_from_pixels(renderer, wl_fmt, stride,
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
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vulkan *vulkan = renderer->vulkan;
	if (!vulkan) {
		return;
	}

	if (renderer->pipeline) {
		vkDestroyPipeline(vulkan->dev, renderer->pipeline, NULL);
	}

	if (renderer->pipeline_layout) {
		vkDestroyPipelineLayout(vulkan->dev, renderer->pipeline_layout, NULL);
	}

	if (renderer->descriptor_set_layout) {
		vkDestroyDescriptorSetLayout(vulkan->dev,
			renderer->descriptor_set_layout, NULL);
	}

	if (renderer->sampler) {
		vkDestroySampler(vulkan->dev, renderer->sampler, NULL);
	}

	if (renderer->render_pass) {
		vkDestroyRenderPass(vulkan->dev, renderer->render_pass, NULL);
	}

	if (renderer->command_pool) {
		vkDestroyCommandPool(vulkan->dev, renderer->command_pool, NULL);
	}

	if (renderer->descriptor_pool) {
		vkDestroyDescriptorPool(vulkan->dev, renderer->descriptor_pool, NULL);
	}

	wlr_vulkan_destroy(renderer->vulkan);
	free(renderer);
}

static void vulkan_init_wl_display(struct wlr_renderer* wlr_renderer,
		struct wl_display* wl_display) {
	// TODO
	// probably have to implement wl_offscreen ourselves since mesa
	// still depends on it
}

static struct wlr_render_surface *vulkan_create_offscreen_render_surface(
		struct wlr_vk_renderer *renderer, void *handle,
		uint32_t width, uint32_t height) {
	VkResult res;
	struct wlr_vulkan *vulkan = renderer->vulkan;
	struct wlr_vk_offscreen_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->vk_render_surface.renderer = renderer;
	rs->vk_render_surface.width = width;
	rs->vk_render_surface.height = height;
	rs->drm_surface = (struct wlr_drm_surface *)handle;
	wlr_render_surface_init(&rs->vk_render_surface.render_surface,
		&offscreen_render_surface_impl);

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
	// implementation notes: we currently create buffers with
	// gbm and then import them to vulkan. Creating images with vulkan
	// and then importing them as gbm bo is probably possible as well.
	for(unsigned i = 0u; i < 2; ++i) {
		struct wlr_vk_offscreen_buffer *buf = &rs->buffers[i];

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
	offscreen_render_surface_destroy(&rs->vk_render_surface.render_surface);
	return NULL;
}

static struct wlr_render_surface *vulkan_create_render_surface(
		struct wlr_renderer *wlr_renderer, void *handle,
		uint32_t width, uint32_t height) {

	VkResult res;
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	if (wlr_backend_is_drm(renderer->backend)) {
		return vulkan_create_offscreen_render_surface(renderer, handle,
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

// returns false on error
// cleanup is done by destroying the renderer
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
		return false;
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
	sampler_info.minLod = 0.f;
	sampler_info.maxLod = 0.25f;
	sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

	res = vkCreateSampler(dev, &sampler_info, NULL, &renderer->sampler);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create sampler", res);
		return false;
	}

	// layouts
	// descriptor set
	VkDescriptorSetLayoutBinding ds_bindings[1] = {{
		0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		VK_SHADER_STAGE_FRAGMENT_BIT, &renderer->sampler
	}};

	VkDescriptorSetLayoutCreateInfo ds_info = {0};
	ds_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	ds_info.bindingCount = 1;
	ds_info.pBindings = ds_bindings;

	res = vkCreateDescriptorSetLayout(dev, &ds_info, NULL,
		&renderer->descriptor_set_layout);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "Failed to create descriptor set layout: %d", res);
		return false;
	}

	// pipeline layout
	VkPushConstantRange pc_range = {0};
	pc_range.size = sizeof(float) * 16; // mat4, see texture.vert
	pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkPipelineLayoutCreateInfo pl_info = {0};
	pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_info.setLayoutCount = 1;
	pl_info.pSetLayouts = &renderer->descriptor_set_layout;
	pl_info.pushConstantRangeCount = 1;
	pl_info.pPushConstantRanges = &pc_range;

	res = vkCreatePipelineLayout(dev, &pl_info, NULL, &renderer->pipeline_layout);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create pipeline layout", res);
		return false;
	}

	// pipeline shader frag
	VkShaderModuleCreateInfo frag_info = {0};
	frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	frag_info.codeSize = sizeof(texture_frag_data);
	frag_info.pCode = texture_frag_data;

	VkShaderModule frag_module = NULL;
	VkShaderModule vert_module = NULL;

	res = vkCreateShaderModule(dev, &frag_info, NULL, &frag_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create fragment shader module", res);
		goto cleanup_shaders;
	}

	// vert
	VkShaderModuleCreateInfo vert_info = {0};
	vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vert_info.codeSize = sizeof(texture_vert_data);
	vert_info.pCode = texture_vert_data;

	res = vkCreateShaderModule(dev, &vert_info, NULL, &vert_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create vertex shader module", res);
		goto cleanup_shaders;
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
		goto cleanup_shaders;
	}

	vkDestroyShaderModule(dev, frag_module, NULL);
	vkDestroyShaderModule(dev, vert_module, NULL);
	return true;

cleanup_shaders:
	if (frag_module) {
		vkDestroyShaderModule(dev, frag_module, NULL);
	}

	if (vert_module) {
		vkDestroyShaderModule(dev, vert_module, NULL);
	}
	return false;
}

struct wlr_renderer *wlr_vk_renderer_create(struct wlr_backend *backend) {
	bool debug = true;
	int ini_ext_count;
	const char **ini_exts;

	// TODO: when getting a offscreen backend, we should use the same gpu as
	// the backend. Not sure if there is a reliable (or even mesa)
	// way to detect if a VkPhysicalDevice is the same as a gbm_device
	// (or offscreen fd) though.
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

	// command pool
	VkCommandPoolCreateInfo cpool_info = {0};
	cpool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpool_info.queueFamilyIndex = vulkan->graphics_queue_fam;
	res = vkCreateCommandPool(vulkan->dev, &cpool_info, NULL,
		&renderer->command_pool);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateCommandPool", res);
		goto error;
	}

	// descriptor pool
	// TODO: at the moment we just allocate one large pool but it might
	// run out. We need dynamic pool creation/allocation algorithm with
	// a list of managed (usage tracked) descriptor pool
	VkDescriptorPoolSize pool_size = {0};
	pool_size.descriptorCount = 100;
	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dpool_info = {0};
	dpool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpool_info.maxSets = 100;
	dpool_info.poolSizeCount = 1;
	dpool_info.pPoolSizes = &pool_size;
	dpool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	res = vkCreateDescriptorPool(vulkan->dev, &dpool_info, NULL,
		&renderer->descriptor_pool);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateDescriptorPool", res);
		goto error;
	}

	// init renderpass, pipeline etc
	if (!init_pipeline(renderer)) {
		wlr_log(WLR_ERROR, "Could not init vulkan pipeline");
		goto error;
	}

	return &renderer->wlr_renderer;

error:
	vulkan_destroy(&renderer->wlr_renderer);
	return NULL;
}
