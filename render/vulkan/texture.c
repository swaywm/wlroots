#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>
#include <render/vulkan.h>

static const struct wlr_texture_impl texture_impl;

struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture) {
	assert(wlr_texture->impl == &texture_impl);
	return (struct wlr_vk_texture *)wlr_texture;
}

static void vulkan_texture_get_size(struct wlr_texture *wlr_texture, int *width,
		int *height) {
	struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);
	*width = texture->width;
	*height = texture->height;
}

static bool vulkan_texture_is_opaque(struct wlr_texture *wlr_texture) {
	struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);
	return !texture->has_alpha;
}

static bool vulkan_texture_write_pixels(struct wlr_texture *wlr_texture,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width,
		uint32_t height, uint32_t src_x, uint32_t src_y, uint32_t dst_x,
		uint32_t dst_y, const void *vdata) {
	VkResult res;
	struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);
	if (texture->format->wl_format != wl_fmt) {
		wlr_log(WLR_ERROR, "vulkan_texture_write_pixels cannot change format");
		return false;
	}

	assert(width <= texture->width && height <= texture->height &&
		src_x >= 0 && src_y >= 0);

	struct wlr_vulkan *vulkan = texture->renderer->vulkan;
	unsigned img_stride = texture->subres_layout.rowPitch;
	unsigned bytespp = texture->format->bpp / 8;

	// TODO: only map really filled region
	unsigned map_size = texture->subres_layout.offset +
		texture->height * img_stride;

	void *vmap;
	res = vkMapMemory(vulkan->dev, texture->memory, 0, map_size, 0, &vmap);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkMapMemory", res);
		return false;
	}

	char *map = (char*) vmap;
	char *data = (char*) vdata;
	map += texture->subres_layout.offset;
	map += dst_x * bytespp + dst_y * img_stride;
	data += src_x * bytespp + dst_y * stride;

	assert(src_x + width <= texture->width);
	assert(src_y + height <= texture->height);
	assert(dst_x + width <= texture->width);
	assert(dst_y + height <= texture->height);

	if (src_x == 0 && width == texture->width && stride == img_stride) {
		memcpy(map, data, stride * height);
	} else {
		for(unsigned i = 0; i < height; ++i) {
			memcpy(map, data, bytespp * width);
			map += img_stride;
			data += stride;
		}
	}

	vkUnmapMemory(vulkan->dev, texture->memory);

	return true;
}

static bool vulkan_texture_to_dmabuf(struct wlr_texture *wlr_texture,
		struct wlr_dmabuf_attributes *attribs) {
	// struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);
	return false;
}

static void vulkan_texture_destroy(struct wlr_texture *wlr_texture) {
	if (wlr_texture == NULL) {
		return;
	}

	struct wlr_vk_texture *texture = vulkan_get_texture(wlr_texture);
	if (!texture->renderer) {
		return;
	}

	struct wlr_vulkan *vulkan = texture->renderer->vulkan;
	if (texture->ds) {
		vkFreeDescriptorSets(vulkan->dev,
			texture->renderer->descriptor_pool, 1, &texture->ds);
	}

	if (texture->image_view) {
		vkDestroyImageView(vulkan->dev, texture->image_view, NULL);
	}

	if (texture->image) {
		vkDestroyImage(vulkan->dev, texture->image, NULL);
	}

	if (texture->memory) {
		vkFreeMemory(vulkan->dev, texture->memory, NULL);
	}

	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.get_size = vulkan_texture_get_size,
	.is_opaque = vulkan_texture_is_opaque,
	.write_pixels = vulkan_texture_write_pixels,
	.to_dmabuf = vulkan_texture_to_dmabuf,
	.destroy = vulkan_texture_destroy,
};

struct wlr_texture *wlr_vk_texture_from_pixels(struct wlr_vk_renderer *renderer,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width,
		uint32_t height, const void *data) {
	VkResult res;
	struct wlr_vulkan *vulkan = renderer->vulkan;

	const struct wlr_vk_pixel_format *fmt = get_vulkan_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(WLR_ERROR, "Unsupported pixel format %"PRIu32, wl_fmt);
		return NULL;
	}

	struct wlr_vk_texture *texture =
		calloc(1, sizeof(struct wlr_vk_texture));
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->renderer = renderer;
	texture->width = width;
	texture->height = height;
	texture->format = fmt;
	texture->has_alpha = fmt->has_alpha;

	// image
	// TODO: using linear image layout on host visible memory
	// is probably really a performance hit. Use staging copy
	// buffers, optimal layout and deviceLocal memory instead.
	// (needs deferred uploading logic, _without_ waiting for device everytime
	// we upload pixels)
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkImageCreateInfo img_info = {0};
	img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	img_info.imageType = VK_IMAGE_TYPE_2D;
	img_info.format = fmt->vk_format;
	img_info.mipLevels = 1;
	img_info.arrayLayers = 1;
	img_info.samples = VK_SAMPLE_COUNT_1_BIT;
	img_info.tiling = VK_IMAGE_TILING_LINEAR;
	img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	img_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
	img_info.extent = (VkExtent3D) { width, height, 1 };
	img_info.usage = usage;
	res = vkCreateImage(vulkan->dev, &img_info, NULL, &texture->image);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateImage failed", res);
		goto error;
	}

	// memory
	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(vulkan->dev, texture->image, &mem_reqs);

	VkMemoryAllocateInfo mem_info = {0};
    mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_info.allocationSize = mem_reqs.size;
	mem_info.memoryTypeIndex = wlr_vulkan_find_mem_type(vulkan,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, mem_reqs.memoryTypeBits);
	res = vkAllocateMemory(vulkan->dev, &mem_info, NULL, &texture->memory);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocatorMemory failed", res);
		goto error;
	}

	res = vkBindImageMemory(vulkan->dev, texture->image, texture->memory, 0);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkBindMemory failed", res);
		goto error;
	}

	// view
	VkImageViewCreateInfo view_info = {0};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = fmt->vk_format;
	view_info.components.r = VK_COMPONENT_SWIZZLE_R;
	view_info.components.g = VK_COMPONENT_SWIZZLE_G;
	view_info.components.b = VK_COMPONENT_SWIZZLE_B;
	view_info.components.a = VK_COMPONENT_SWIZZLE_A;
	if (!fmt->has_alpha) {
		view_info.components.a = VK_COMPONENT_SWIZZLE_ONE;
	}

	view_info.subresourceRange = (VkImageSubresourceRange) {
		VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
	};
	view_info.image = texture->image;
	res = vkCreateImageView(vulkan->dev, &view_info, NULL,
		&texture->image_view);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateImageView failed", res);
		goto error;
	}

	// layout for pixel writing
	VkImageSubresource subres = {0};
	subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vkGetImageSubresourceLayout(vulkan->dev, texture->image,
		&subres, &texture->subres_layout);

	// descriptor set
	VkDescriptorSetAllocateInfo ds_info = {0};
	ds_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ds_info.descriptorPool = renderer->descriptor_pool;
	ds_info.descriptorSetCount = 1;
	ds_info.pSetLayouts = &renderer->descriptor_set_layout;
	res = vkAllocateDescriptorSets(vulkan->dev, &ds_info, &texture->ds);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateDescriptorSets", res);
		goto error;
	}

	VkDescriptorImageInfo ds_img_info = {0};
	ds_img_info.imageView = texture->image_view;
	ds_img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	ds_img_info.sampler = renderer->sampler;

	VkWriteDescriptorSet ds_write = {0};
	ds_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	ds_write.descriptorCount = 1;
	ds_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	ds_write.dstSet = texture->ds;
	ds_write.pImageInfo = &ds_img_info;

	vkUpdateDescriptorSets(vulkan->dev, 1, &ds_write, 0, NULL);
	// write data
	if (!vulkan_texture_write_pixels(&texture->wlr_texture, wl_fmt, stride,
			width, height, 0, 0, 0, 0, data)) {
		goto error;
	}
	// change image layout
	VkCommandBuffer cb = renderer->staging.cb;
	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(cb, &begin_info);
	vulkan_change_layout(cb, texture->image,
		VK_IMAGE_LAYOUT_PREINITIALIZED, VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
	vkEndCommandBuffer(cb);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pCommandBuffers = &cb;
	submit_info.commandBufferCount = 1;

	res = vkQueueSubmit(vulkan->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
	if (res != VK_SUCCESS) {
		vkFreeCommandBuffers(vulkan->dev, renderer->command_pool, 1u, &cb);
		wlr_vulkan_error("vkQueueSubmit", res);
		goto error;
	}

	vkDeviceWaitIdle(vulkan->dev);
	return &texture->wlr_texture;

error:
	vulkan_texture_destroy(&texture->wlr_texture);
	return NULL;
}
