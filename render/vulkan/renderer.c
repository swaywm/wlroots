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

#include "shaders/texture.vert.h"
#include "shaders/texture.frag.h"

#define wlr_vulkan_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

static const struct wlr_renderer_impl renderer_impl;

static struct wlr_vk_renderer *vulkan_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_vk_renderer *)wlr_renderer;
}

// renderer
static void vulkan_begin(struct wlr_renderer *renderer,
		uint32_t width, uint32_t height) {
	// something like: retrieve swapchain from output,
	// acquire image and begin recording the command buffer
	// maybe acquire image later on
}

static void vulkan_end(struct wlr_renderer *renderer) {
	// finish command buffer recording (& renderpass)
	// maybe acquire image here (if not in begin)
	// present image to swapchain
}

static bool vulkan_render_texture_with_matrix(struct wlr_renderer *renderer,
		struct wlr_texture *texture, const float matrix[9], float alpha) {
	// bind pipeline (if not already bound)
	// write matrix into ubo, update descriptor set for texture
	//  even better: every texture has its own descriptor set (with bound image)
	// render shit

	return true;
}

static void vulkan_destroy(struct wlr_renderer *renderer) {
	// if (vulkan->wl_drm) {
	// 	wl_global_destroy(vulkan->wl_drm);
	// }

	// TODO
}

static void vulkan_clear(struct wlr_renderer *renderer,
		const float color[static 4]) {
}

static void vulkan_scissor(struct wlr_renderer *renderer,
		struct wlr_box *box) {
}

static bool vulkan_resource_is_wl_drm_buffer(struct wlr_renderer *renderer,
		struct wl_resource *resource) {
	return false;
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
	return wlr_vulkan_texture_from_pixels(renderer->vulkan, wl_fmt, stride,
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

static void vulkan_init_wl_display(struct wlr_renderer* wlr_renderer,
		struct wl_display* wl_display) {
}

static const struct wlr_renderer_impl renderer_impl = {
	.begin = vulkan_begin,
	.end = vulkan_end,
	.clear = vulkan_clear,
	.scissor = vulkan_scissor,
	.render_texture_with_matrix = vulkan_render_texture_with_matrix,
	.render_quad_with_matrix = vulkan_render_quad_with_matrix,
	.render_ellipse_with_matrix = vulkan_render_ellipse_with_matrix,
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
};

static bool init_pipeline(struct wlr_vk_renderer *renderer) {
	// util
	VkDevice dev = renderer->vulkan->dev;
	VkResult res;

	// TODO: correct formats (from swapchain)
	// renderpass
	VkAttachmentDescription attachment = {
		0, // flags
		VK_FORMAT_B8G8R8A8_UNORM, // format
		VK_SAMPLE_COUNT_1_BIT, // samples
		VK_ATTACHMENT_LOAD_OP_CLEAR, // loadOp
		VK_ATTACHMENT_STORE_OP_STORE, // storeOp
		VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencil load
		VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencil store
		VK_IMAGE_LAYOUT_UNDEFINED, // initial layout
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, // final layout
	};

	VkAttachmentReference color_ref = {
		0, // attachment
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
	};

	VkSubpassDescription subpass = {
		0, // flags
		VK_PIPELINE_BIND_POINT_GRAPHICS, // bind pint
		0, // input attachments
		NULL,
		1, // color attachments
		&color_ref,
		NULL, // resolve attachment
		NULL, // depth attachment
		0, // preserve attachments
		NULL
	};

	VkRenderPassCreateInfo rp_info = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		NULL, // pnext
		0, // flags
		1, // attachment count
		&attachment, // pAttachments
		1, // subpassCount
		&subpass, // pSubpasses
		0, // dependencies
		NULL
	};

	res = vkCreateRenderPass(dev, &rp_info, NULL, &renderer->render_pass);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create render pass", res);
		goto error;
	}

	// sampler
	VkSamplerCreateInfo sampler_info = {
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		NULL, // pnext
		0, // flags
		VK_FILTER_LINEAR, // magFilter
		VK_FILTER_LINEAR, // minFilter
		VK_SAMPLER_MIPMAP_MODE_NEAREST, // mipmapMode
		VK_SAMPLER_ADDRESS_MODE_REPEAT, // addressu
		VK_SAMPLER_ADDRESS_MODE_REPEAT, // addressv
		VK_SAMPLER_ADDRESS_MODE_REPEAT, // addressv
		0, // lodBias
		false, // anisotropy
		1.0, // maxAnisotrpy
		false, // compareEnable
		VK_COMPARE_OP_ALWAYS, // compareOp
		0, // minLod
		0.25, // maxLod
		VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, // borderColor
		false, // unnormalizedCoordinates
	};

	res = vkCreateSampler(dev, &sampler_info, NULL, &renderer->sampler);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create sampler", res);
		goto error;
	}

	// layouts
	// descriptor set
	VkDescriptorSetLayoutBinding ds_bindings[2] = {{
		0, // binding
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // type
		1, // count
		VK_SHADER_STAGE_VERTEX_BIT, // stage flags
		NULL,
	}, {
		1, // binding
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // type
		1, // count
		VK_SHADER_STAGE_FRAGMENT_BIT, // stage flags
		&renderer->sampler // immutable sampler
	}};

	VkDescriptorSetLayoutCreateInfo ds_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		NULL, // pnext
		0, // flags
		2, // binding count
		ds_bindings // bindings
	};

	res = vkCreateDescriptorSetLayout(dev, &ds_info, NULL,
		&renderer->descriptor_set_layout);
	if (res != VK_SUCCESS) {
		wlr_log(WLR_ERROR, "Failed to create descriptor set layout: %d", res);
		goto error;
	}

	// pipeline
	VkPipelineLayoutCreateInfo pl_info = {
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		NULL, // pnext
		0, // flags
		1, // setLayoutCount
		&renderer->descriptor_set_layout, // setLayouts
		0, // pushRangesCount
		NULL // pushRanges
	};

	res = vkCreatePipelineLayout(dev, &pl_info, NULL, &renderer->pipeline_layout);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create pipeline layout", res);
		goto error;
	}

	// pipeline
	// shader
	// frag
	size_t frag_size = sizeof(texture_frag_data) / sizeof(texture_frag_data[0]);
	VkShaderModuleCreateInfo frag_info = {
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		frag_size, // codeSize
		texture_frag_data // pCode
	};

	VkShaderModule frag_module;
	res = vkCreateShaderModule(dev, &frag_info, NULL, &frag_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create fragment shader module", res);
		goto error;
	}

	// vert
	size_t vert_size = sizeof(texture_vert_data) / sizeof(texture_vert_data[0]);
	VkShaderModuleCreateInfo vert_info = {
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		vert_size, // codeSize
		texture_vert_data // pCode
	};

	VkShaderModule vert_module;
	res = vkCreateShaderModule(dev, &vert_info, NULL, &vert_module);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create vertex shader module", res);
		goto error;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		VK_SHADER_STAGE_VERTEX_BIT, // stage
		vert_module, // module
		"main", // name
		NULL // specialization info
	}, {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		VK_SHADER_STAGE_FRAGMENT_BIT, // stage
		frag_module, // module
		"main", // name
		NULL // specialization info
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

	// TODO: cache
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


struct wlr_renderer *wlr_vk_renderer_create(struct wlr_vulkan *vulkan) {
	struct wlr_vk_renderer *renderer;
	VkResult res;
	if (!(renderer = calloc(1, sizeof(*renderer)))) {
		wlr_log_errno(WLR_ERROR, "failed to allocate wlr_vk_renderer");
		return NULL;
	}

	renderer->vulkan = vulkan;
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);

	VkCommandPoolCreateInfo pool_info = {0};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
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
