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

#include "shaders/common.vert.h"
#include "shaders/texture.frag.h"
#include "shaders/quad.frag.h"
#include "shaders/ellipse.frag.h"

static const struct wlr_renderer_impl renderer_impl;

struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_vk_renderer *)wlr_renderer;
}

// renderer
// util
static void mat3_to_mat4(const float mat3[9], float mat4[4][4]) {
	memset(mat4, 0, sizeof(float) * 16);
	mat4[0][0] = mat3[0];
	mat4[0][1] = mat3[1];
	mat4[0][3] = mat3[2];

	mat4[1][0] = mat3[3];
	mat4[1][1] = mat3[4];
	mat4[1][3] = mat3[5];

	mat4[2][2] = 1.f;
	mat4[3][3] = 1.f;
}

static void vulkan_destroy_staging(struct wlr_vk_renderer *r) {
	if (r->staging.buffer) {
		vkDestroyBuffer(r->vulkan->dev, r->staging.buffer, NULL);
		r->staging.buffer = VK_NULL_HANDLE;
	}
	if (r->staging.memory) {
		vkFreeMemory(r->vulkan->dev, r->staging.memory, NULL);
		r->staging.memory = VK_NULL_HANDLE;
	}
}

VkBuffer wlr_vk_renderer_get_staging_buffer(struct wlr_vk_renderer *r,
		size_t size) {
	if (r->staging.size >= size) {
		return r->staging.buffer;
	}

	size *= 2;
	size_t min = 32 * 1024;
	size = size < min ? min : size;

	struct wlr_vulkan *vulkan = r->vulkan;
	vulkan_destroy_staging(r);

	VkResult res;
	VkBufferCreateInfo buf_info = {0};
	buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buf_info.size = size;
	buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	res = vkCreateBuffer(vulkan->dev, &buf_info, NULL, &r->staging.buffer);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateBuffer", res);
		return VK_NULL_HANDLE;
	}

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(vulkan->dev, r->staging.buffer, &mem_reqs);

	VkMemoryAllocateInfo mem_info = {0};
	mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_info.allocationSize = mem_reqs.size;
	mem_info.memoryTypeIndex = wlr_vulkan_find_mem_type(vulkan,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mem_reqs.memoryTypeBits);
	res = vkAllocateMemory(vulkan->dev, &mem_info, NULL, &r->staging.memory);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocatorMemory", res);
		return VK_NULL_HANDLE;
	}

	res = vkBindBufferMemory(vulkan->dev, r->staging.buffer,
		r->staging.memory, 0);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkBindBufferMemory", res);
		return VK_NULL_HANDLE;
	}

	return r->staging.buffer;
}

// interface implementation
static bool vulkan_begin(struct wlr_renderer *wlr_renderer,
		struct wlr_render_surface *wlr_rs) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vk_render_surface *rs = vulkan_get_render_surface(wlr_rs);
	assert(!renderer->current);
	assert(rs->renderer == renderer);

	VkCommandBuffer cb = rs->cb;
	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(cb, &begin_info);

	VkFramebuffer fb = vulkan_render_surface_begin(rs, cb);
	if (!fb) {
		vkEndCommandBuffer(cb);
		return false;
	}

	uint32_t width = rs->rs.width;
	uint32_t height = rs->rs.height;
	VkRect2D rect = {{0, 0}, {width, height}};
	renderer->scissor = rect;

	VkRenderPassBeginInfo rp_info = {0};
	rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_info.renderArea = rect;
	rp_info.renderPass = renderer->render_pass;
	rp_info.framebuffer = fb;
	rp_info.clearValueCount = 0;
	vkCmdBeginRenderPass(cb, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0.f, 0.f, (float) width, (float) height, 0.f, 1.f};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &rect);

	renderer->current = rs;
	return true;
}

static void vulkan_end(struct wlr_renderer *wlr_renderer) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;


	vkCmdEndRenderPass(cb);
	vkEndCommandBuffer(cb);

	if (!vulkan_render_surface_end(renderer->current, cb)) {
		goto clean;
	}

	// :(
	// sadly this is required due to the current api/rendering model of wlr
	// ideally we could use gpu and cpu in parallel (_without_ the
	// implicit synchronization overhead and mess of opengl drivers)
	vkDeviceWaitIdle(renderer->vulkan->dev);

clean:
	renderer->current = NULL;
}

static bool vulkan_render_texture_with_matrix(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *wlr_texture, const float matrix[9], float alpha) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->tex_pipe);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->pipeline_layout, 0, 1, &texture->ds, 0, NULL);

	float mat4[4][4] = {0};
	mat3_to_mat4(matrix, mat4);
	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mat4);
	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 16 * sizeof(float), sizeof(float),
		&alpha);
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
	rect.rect = renderer->scissor;
	rect.layerCount = 1;
	vkCmdClearAttachments(cb, 1, &att, 1, &rect);
}

static void vulkan_scissor(struct wlr_renderer *wlr_renderer,
		struct wlr_box *box) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	uint32_t w = renderer->current->rs.width;
	uint32_t h = renderer->current->rs.height;
	VkRect2D rect = {{0, 0}, {w, h}};
	if (box) {
		rect = (VkRect2D) {{box->x, box->y}, {box->width, box->height}};
	}

	renderer->scissor = rect;
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
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->quad_pipe);

	float mat4[4][4] = {0};
	mat3_to_mat4(matrix, mat4);
	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mat4);
	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 16 * sizeof(float), sizeof(float) * 4,
		color);
	vkCmdDraw(cb, 4, 1, 0, 0);
}

static void vulkan_render_ellipse_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	assert(renderer->current);
	VkCommandBuffer cb = renderer->current->cb;

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		renderer->ellipse_pipe);

	float mat4[4][4] = {0};
	mat3_to_mat4(matrix, mat4);
	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, mat4);
	vkCmdPushConstants(cb, renderer->pipeline_layout,
		VK_SHADER_STAGE_FRAGMENT_BIT, 16 * sizeof(float), sizeof(float) * 4,
		color);
	vkCmdDraw(cb, 4, 1, 0, 0);
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
	wlr_log(WLR_ERROR, "vulkan wl_drm support not implemented");
	return NULL;
}

static struct wlr_texture *vulkan_texture_from_dmabuf(
		struct wlr_renderer *wlr_renderer,
		struct wlr_dmabuf_attributes *attribs) {
	wlr_log(WLR_ERROR, "vulkan dmabuf support not implemented");
	return NULL;
}

static void vulkan_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vulkan *vulkan = renderer->vulkan;
	if (!vulkan) {
		return;
	}

	vulkan_destroy_staging(renderer);
	if (renderer->tex_pipe) {
		vkDestroyPipeline(vulkan->dev, renderer->tex_pipe, NULL);
	}
	if (renderer->quad_pipe) {
		vkDestroyPipeline(vulkan->dev, renderer->quad_pipe, NULL);
	}
	if (renderer->ellipse_pipe) {
		vkDestroyPipeline(vulkan->dev, renderer->ellipse_pipe, NULL);
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

static void vulkan_init_wl_display(struct wlr_renderer *wlr_renderer,
		struct wl_display *wl_display) {
	// probably have to implement wl_offscreen ourselves since mesa
	// still depends on it
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
	.format_supported = vulkan_format_supported,
	.texture_from_pixels = vulkan_texture_from_pixels,
	.texture_from_wl_drm = vulkan_texture_from_wl_drm,
	.texture_from_dmabuf = vulkan_texture_from_dmabuf,
	.destroy = vulkan_destroy,
	.init_wl_display = vulkan_init_wl_display,
	.render_surface_create_wl = vulkan_render_surface_create_wl,
	.render_surface_create_xcb = vulkan_render_surface_create_xcb,
	.render_surface_create_gbm = vulkan_render_surface_create_gbm,
	.render_surface_create_headless = vulkan_render_surface_create_headless,
};

// returns false on error
// cleanup is done by destroying the renderer
static bool init_pipelines(struct wlr_vk_renderer *renderer) {
	// util
	VkDevice dev = renderer->vulkan->dev;
	VkResult res;

	// TODO: better to use a format we know we can create swapchains for
	// although bgra8 is usually supported, it might not be
	// renderpass
	VkAttachmentDescription attachment = {0};
	attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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

	deps[1].srcSubpass = 0;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT |
		VK_PIPELINE_STAGE_HOST_BIT;
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
	VkPushConstantRange pc_ranges[2] = {0};
	pc_ranges[0].size = sizeof(float) * 16; // mat4, see texture.vert
	pc_ranges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	pc_ranges[1].offset = sizeof(float) * 16;
	pc_ranges[1].size = sizeof(float) * 4; // alpha or color
	pc_ranges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkPipelineLayoutCreateInfo pl_info = {0};
	pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl_info.setLayoutCount = 1;
	pl_info.pSetLayouts = &renderer->descriptor_set_layout;
	pl_info.pushConstantRangeCount = 2;
	pl_info.pPushConstantRanges = pc_ranges;

	res = vkCreatePipelineLayout(dev, &pl_info, NULL, &renderer->pipeline_layout);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create pipeline layout", res);
		return false;
	}

	// shaders
	VkShaderModule tex_frag_module = NULL;
	VkShaderModule quad_frag_module = NULL;
	VkShaderModule ellipse_frag_module = NULL;
	VkShaderModule vert_module = NULL;

	// common vert
	VkShaderModuleCreateInfo sinfo = {0};
	sinfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	sinfo.codeSize = sizeof(common_vert_data);
	sinfo.pCode = common_vert_data;
	res = vkCreateShaderModule(dev, &sinfo, NULL, &vert_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create vertex shader module", res);
		goto cleanup_shaders;
	}

	VkPipelineShaderStageCreateInfo vert_stage = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vert_module, "main", NULL
	};

	// tex frag
	sinfo.codeSize = sizeof(texture_frag_data);
	sinfo.pCode = texture_frag_data;
	res = vkCreateShaderModule(dev, &sinfo, NULL, &tex_frag_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create tex fragment shader module", res);
		goto cleanup_shaders;
	}

	// quad frag
	sinfo.codeSize = sizeof(quad_frag_data);
	sinfo.pCode = quad_frag_data;
	res = vkCreateShaderModule(dev, &sinfo, NULL, &quad_frag_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create quad fragment shader module", res);
		goto cleanup_shaders;
	}

	// ellipse frag
	sinfo.codeSize = sizeof(ellipse_frag_data);
	sinfo.pCode = ellipse_frag_data;
	res = vkCreateShaderModule(dev, &sinfo, NULL, &ellipse_frag_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create ellipse fragment shader module", res);
		goto cleanup_shaders;
	}

	VkPipelineShaderStageCreateInfo tex_stages[2] = {vert_stage, {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, tex_frag_module, "main", NULL
	}};

	VkPipelineShaderStageCreateInfo quad_stages[2] = {vert_stage, {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, quad_frag_module, "main", NULL
	}};

	VkPipelineShaderStageCreateInfo ellipse_stages[2] = {vert_stage, {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ellipse_frag_module, "main", NULL
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
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
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

	VkGraphicsPipelineCreateInfo pinfos[3] = {0};
	pinfos[0].sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pinfos[0].layout = renderer->pipeline_layout;
	pinfos[0].renderPass = renderer->render_pass;
	pinfos[0].subpass = 0;
	pinfos[0].stageCount = 2;
	pinfos[0].pStages = tex_stages;

	pinfos[0].pInputAssemblyState = &assembly;
	pinfos[0].pRasterizationState = &rasterization;
	pinfos[0].pColorBlendState = &blend;
	pinfos[0].pMultisampleState = &multisample;
	pinfos[0].pViewportState = &viewport;
	pinfos[0].pDynamicState = &dynamic;
	pinfos[0].pVertexInputState = &vertex;

	pinfos[1] = pinfos[0];
	pinfos[1].pStages = quad_stages;

	pinfos[2] = pinfos[0];
	pinfos[2].pStages = ellipse_stages;

	// NOTE: use could use a cache here for faster loading
	// store it somewhere like $XDG_CACHE_HOME/wlroots/vk_pipe_cache
	VkPipelineCache cache = VK_NULL_HANDLE;
	VkPipeline pipes[3] = {0};
	res = vkCreateGraphicsPipelines(dev, cache, 3, pinfos, NULL, pipes);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "failed to create vulkan pipelines: %d", res);
		goto cleanup_shaders;
	}

	renderer->tex_pipe = pipes[0];
	renderer->quad_pipe = pipes[1];
	renderer->ellipse_pipe = pipes[2];

	vkDestroyShaderModule(dev, tex_frag_module, NULL);
	vkDestroyShaderModule(dev, quad_frag_module, NULL);
	vkDestroyShaderModule(dev, ellipse_frag_module, NULL);
	vkDestroyShaderModule(dev, vert_module, NULL);
	return true;

cleanup_shaders:
	if (tex_frag_module) {
		vkDestroyShaderModule(dev, tex_frag_module, NULL);
	}
	if (quad_frag_module) {
		vkDestroyShaderModule(dev, quad_frag_module, NULL);
	}
	if (ellipse_frag_module) {
		vkDestroyShaderModule(dev, ellipse_frag_module, NULL);
	}
	if (vert_module) {
		vkDestroyShaderModule(dev, vert_module, NULL);
	}
	return false;
}

struct wlr_renderer *wlr_vk_renderer_create(struct wlr_backend *backend) {
	bool debug = true;

	wlr_log(WLR_ERROR, "The vulkan renderer is only experimental and "
		"not expected to be ready for daliy use");

	// TODO: when getting a drm backend, we should use the same gpu as
	// the backend. Not sure if there is a reliable (or even mesa-only)
	// way to detect if a VkPhysicalDevice is the same as a gbm_device
	// (or offscreen fd) though.
	struct wlr_vulkan *vulkan = wlr_vulkan_create(0, NULL, 0, NULL, debug);
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
	// a list of managed (usage tracked) descriptor pool.
	// Can't handle more than 500 textures at the moment
	unsigned count = 500;
	VkDescriptorPoolSize pool_size = {0};
	pool_size.descriptorCount = count;
	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

	VkDescriptorPoolCreateInfo dpool_info = {0};
	dpool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpool_info.maxSets = count;
	dpool_info.poolSizeCount = 1;
	dpool_info.pPoolSizes = &pool_size;
	dpool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	res = vkCreateDescriptorPool(vulkan->dev, &dpool_info, NULL,
		&renderer->descriptor_pool);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateDescriptorPool", res);
		goto error;
	}

	// staging command buffer
	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = 1u;
	res = vkAllocateCommandBuffers(renderer->vulkan->dev, &cmd_buf_info,
		&renderer->staging.cb);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		goto error;
	}

	// init renderpass, pipeline etc
	if (!init_pipelines(renderer)) {
		wlr_log(WLR_ERROR, "Could not init vulkan pipeline");
		goto error;
	}

	return &renderer->wlr_renderer;

error:
	vulkan_destroy(&renderer->wlr_renderer);
	return NULL;
}
