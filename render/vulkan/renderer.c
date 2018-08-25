#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
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
static const struct wlr_render_surface_impl render_surface_impl;

static struct wlr_vk_renderer *vulkan_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_vk_renderer *)wlr_renderer;
}

static struct wlr_vk_render_surface *vulkan_get_render_surface(
		struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &render_surface_impl);
	return (struct wlr_vk_render_surface *)wlr_rs;
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

// render surface
static bool vulkan_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_vk_render_surface *rs = vulkan_get_render_surface(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->renderer;
	assert(renderer->current == rs); // TODO: should not be assert
	VkResult res;

	// present
	VkPresentInfoKHR present_info = {0};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &renderer->current->swapchain.swapchain;
	present_info.pImageIndices = &renderer->current_id;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &renderer->present;

	res = vkQueuePresentKHR(renderer->vulkan->present_queue, &present_info);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkQueuePresentKHR", res);
		goto clean;
	}

	// :(
	// sadly this is required due to the current api/rendering model of wlr
	// ideally we could use gpu and cpu in parallel (_without_ the
	// implicit synchronization overhead and mess of opengl drivers)
	vkDeviceWaitIdle(renderer->vulkan->dev);

clean:
	renderer->current = NULL;
	renderer->current_id = 0xFFFFFFFF;
	return true;
}

static void vulkan_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	// TODO: drm
	struct wlr_vk_render_surface* rs = vulkan_get_render_surface(wlr_rs);
	rs->width = width;
	rs->height = height;
	if (!wlr_vk_swapchain_resize(&rs->swapchain, width, height)) {
		wlr_log(WLR_ERROR, "Failed to resize vulkan render surface");
	}
}

static void vulkan_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
}

static const struct wlr_render_surface_impl render_surface_impl = {
	.destroy = vulkan_render_surface_destroy,
	.swap_buffers = vulkan_swap_buffers,
	.resize = vulkan_render_surface_resize,
};

// renderer
static bool vulkan_begin(struct wlr_renderer *wlr_renderer,
		struct wlr_render_surface *wlr_rs, int *buffer_age) {
	// TODO: needs special handling for drm
	struct wlr_vk_render_surface *rs = vulkan_get_render_surface(wlr_rs);
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vulkan *vulkan = rs->renderer->vulkan;

	// TODO: handle (skip) out of date/suboptimal (only relevant on x11/wl)
	VkResult res;
	uint32_t id;
	res = vkAcquireNextImageKHR(vulkan->dev, rs->swapchain.swapchain,
		0xFFFFFFFF, renderer->acquire, VK_NULL_HANDLE, &id);
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

	fflush(stdout);

	// start recording
	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(rs->swapchain.cb, &begin_info);

	VkRect2D rect = {{0, 0}, {rs->width, rs->height}};
	VkClearValue clear_value = {0};

	VkRenderPassBeginInfo rp_info = {0};
	rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_info.renderArea = rect;
	rp_info.renderPass = renderer->render_pass;
	rp_info.framebuffer = rs->swapchain.buffers[id].framebuffer;
	rp_info.clearValueCount = 1;
	rp_info.pClearValues = &clear_value;
	vkCmdBeginRenderPass(rs->swapchain.cb, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0.f, 0.f, (float) rs->width, (float) rs->height, 0.f, 1.f};
	vkCmdSetViewport(rs->swapchain.cb, 0, 1, &vp);
	vkCmdSetScissor(rs->swapchain.cb, 0, 1, &rect);

	renderer->current = rs;
	renderer->current_id = id;
	return true;
}

static void vulkan_end(struct wlr_renderer *wlr_renderer) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->swapchain.cb;
	VkResult res;

	vkCmdEndRenderPass(cb);
	vkEndCommandBuffer(cb);

	// submit
	VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pWaitSemaphores = &renderer->acquire;
	submit_info.pWaitDstStageMask = &stage;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pCommandBuffers = &cb;
	submit_info.commandBufferCount = 1;
	submit_info.pSignalSemaphores = &renderer->present;
	submit_info.signalSemaphoreCount = 1;

	res = vkQueueSubmit(renderer->vulkan->graphics_queue, 1, &submit_info,
		VK_NULL_HANDLE);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkQueueSubmit", res);
	}

	// we don't present here but in vulkan_swap_buffers (render_surface)
}

static bool vulkan_render_texture_with_matrix(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *texture, const float matrix[9], float alpha) {
	// bind pipeline (if not already bound)
	// write matrix into ubo, update descriptor set for texture
	//  even better: every texture has its own descriptor set (with bound image)
	// render shit

	return true;
}

static void vulkan_clear(struct wlr_renderer *wlr_renderer,
		const float color[static 4]) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->swapchain.cb;

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
}

static void vulkan_render_ellipse_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
}

static void vulkan_wl_drm_buffer_get_size(struct wlr_renderer *wlr_renderer,
		struct wl_resource *buffer, int *width, int *height) {
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

static struct wlr_render_surface *vulkan_create_render_surface(
		struct wlr_renderer *wlr_renderer, void *handle,
		uint32_t width, uint32_t height) {

	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vk_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->renderer = renderer;
	rs->width = width;
	rs->height = height;
	wlr_render_surface_init(&rs->render_surface, &render_surface_impl);
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
		return &rs->render_surface;
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
		return &rs->render_surface;
	}

	free(rs);
	return NULL;

error:
	wlr_render_surface_destroy(&rs->render_surface);
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

	// TODO: correct formats (from swapchain)
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
	deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

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

	// TODO: use cache
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

	VkSemaphoreCreateInfo sem_info = {0};
	sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	res = vkCreateSemaphore(vulkan->dev, &sem_info, NULL,
		&renderer->acquire);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateSemaphore (1)", res);
		goto error;
	}

	res = vkCreateSemaphore(vulkan->dev, &sem_info, NULL,
		&renderer->present);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateSemaphore (2)", res);
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
