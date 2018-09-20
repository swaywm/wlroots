#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <render/vulkan.h>
#include <wlr/util/log.h>
#include <wlr/config.h>

#include <vulkan/vulkan_wayland.h>
#include <gbm.h>

#ifdef WLR_HAS_X11_BACKEND
	#include <xcb/xcb.h>
	#include <vulkan/vulkan_xcb.h>
#endif


// util
static const struct wlr_render_surface_impl swapchain_render_surface_impl;
static const struct wlr_render_surface_impl offscreen_render_surface_impl;

struct wlr_vk_render_surface *vulkan_get_render_surface(
		struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &swapchain_render_surface_impl ||
		wlr_rs->impl == &offscreen_render_surface_impl);
	return (struct wlr_vk_render_surface *)wlr_rs;
}

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

// not used atm
// static bool vulkan_render_surface_is_offscreen(
// 		struct wlr_render_surface *wlr_rs) {
// 	return wlr_rs->impl == &offscreen_render_surface_impl;
// }

static bool vulkan_read_pixels(struct wlr_vk_render_surface *rs,
		VkImage read, enum wl_shm_format wl_fmt, uint32_t *flags,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *vdata) {
	// vulkan has (0,0) is top left corner by default
	(void) flags;
	VkResult res;
	struct wlr_vk_renderer *renderer = rs->renderer;
	struct wlr_vulkan *vulkan = renderer->vulkan;

	if (wl_fmt != WL_SHM_FORMAT_ARGB8888 && wl_fmt != WL_SHM_FORMAT_XRGB8888) {
		wlr_log(WLR_ERROR, "unsupported format");
		return false;
	}

	uint32_t bytespp = 4u; // always using argb (bgra as vulkan format)
	size_t bsize = stride * height;

	struct wlr_vk_buffer_span span = wlr_vk_get_stage_span(renderer, bsize);
	if (!span.buffer) {
		wlr_log(WLR_ERROR, "Failed to retrieve staging buffer");
		return false;
	}

	VkCommandBuffer cb = wlr_vk_record_stage_cb(renderer);
	vulkan_change_layout(cb, read,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT);

	VkBufferImageCopy copy = {0};
	copy.imageExtent = (VkExtent3D) {width, height, 1};
	copy.imageOffset = (VkOffset3D) {src_x, src_y, 0};
	copy.bufferOffset = span.alloc.start;
	copy.bufferRowLength = stride / bytespp;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	vkCmdCopyImageToBuffer(cb, read, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		span.buffer->buffer, 1, &copy);

	vulkan_change_layout(cb, read,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0);

	if (!wlr_vk_submit_stage_wait(renderer)) {
		return false;
	}

	// read staging buffer into data
	void *vmap;
	res = vkMapMemory(vulkan->dev, span.buffer->memory, span.alloc.start,
		bsize, 0, &vmap);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkMapMemory", res);
		return false;
	}

	char *map = (char *)vmap;
	char *data = (char *)vdata;
	data += dst_y * stride;
	data += dst_x * bytespp;
	memcpy(data, map, stride * height);
	vkUnmapMemory(vulkan->dev, span.buffer->memory);

	return true;
}


// swapchain
static void destroy_swapchain_buffers(struct wlr_vk_swapchain *swapchain) {
	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	if (swapchain->image_count == 0) {
		return;
	}

	for (uint32_t i = 0; i < swapchain->image_count; i++) {
		struct wlr_vk_swapchain_buffer *buf = &swapchain->buffers[i];
		if (buf->framebuffer) {
			vkDestroyFramebuffer(vulkan->dev, buf->framebuffer, NULL);
		}

		if (buf->image_view) {
			vkDestroyImageView(vulkan->dev, buf->image_view, NULL);
		}
	}

	swapchain->image_count = 0;
	free(swapchain->buffers);
}

static bool init_swapchain_buffers(struct wlr_vk_swapchain *swapchain) {
	VkResult res;
	struct wlr_vk_renderer *renderer = swapchain->renderer;
	struct wlr_vulkan *vulkan = renderer->vulkan;

	destroy_swapchain_buffers(swapchain);
	res = vkGetSwapchainImagesKHR(vulkan->dev, swapchain->swapchain,
		&swapchain->image_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to get swapchain images (1)", res);
		return false;
	}

	VkImage images[swapchain->image_count];
	res = vkGetSwapchainImagesKHR(vulkan->dev, swapchain->swapchain,
		&swapchain->image_count, images);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to get swapchain images (2)", res);
		return false;
	}

	if (!(swapchain->buffers = calloc(swapchain->image_count,
				sizeof(*swapchain->buffers)))) {
		wlr_log(WLR_ERROR, "Failed to allocate swapchain buffers");
		return false;
	}

	for (uint32_t i = 0; i < swapchain->image_count; i++) {
		struct wlr_vk_swapchain_buffer *buf = &swapchain->buffers[i];

		VkImageViewCreateInfo view_info = {0};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.format = swapchain->create_info.imageFormat;
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
		view_info.image = images[i];

		buf->image = images[i];
		res = vkCreateImageView(vulkan->dev, &view_info, NULL,
			&swapchain->buffers[i].image_view);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateImageView", res);
			return false;
		}

		VkFramebufferCreateInfo fb_info = {0};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments = &buf->image_view;
		fb_info.renderPass = renderer->render_pass;
		fb_info.width = swapchain->create_info.imageExtent.width;
		fb_info.height = swapchain->create_info.imageExtent.height;
		fb_info.layers = 1;

		res = vkCreateFramebuffer(vulkan->dev, &fb_info, NULL,
			&buf->framebuffer);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateFramebuffer", res);
			return false;
		}
	}

	return true;
}

static void wlr_vk_swapchain_finish(struct wlr_vk_swapchain *swapchain) {
	if (!swapchain || !swapchain->renderer) {
		return;
	}

	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	destroy_swapchain_buffers(swapchain);
	if (swapchain->swapchain) {
		vkDestroySwapchainKHR(vulkan->dev, swapchain->swapchain, NULL);
	}

	memset(swapchain, 0, sizeof(*swapchain));
}

static bool wlr_vk_swapchain_init(struct wlr_vk_swapchain *swapchain,
		struct wlr_vk_renderer *renderer,
		VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) {

	VkResult res;
	struct wlr_vulkan *vulkan = renderer->vulkan;
	swapchain->renderer = renderer;
	swapchain->surface = surface;

	// check if we can present on the given device
	// NOTE: instead of creating a random present queue and then just
	// hoping that it works (otherwise failing) we should only
	// really create the renderer when we have (all?) outputs.
	VkBool32 supported;
	res = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan->phdev,
		vulkan->present_queue_fam, surface, &supported);
	if (res != VK_SUCCESS || !supported) {
		wlr_vulkan_error("invalid phdev/present queue for swapchain", res);
		return NULL;
	}

	// swapchain create info
	VkSwapchainCreateInfoKHR info = {0};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = surface;

	VkSurfaceCapabilitiesKHR caps;
	res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->phdev, surface,
		&caps);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("failed retrieve surface caps", res);
		return NULL;
	}

	if (caps.currentExtent.width == (uint32_t)-1) {
		info.imageExtent.width = width;
		info.imageExtent.height = height;
	} else {
		info.imageExtent.width = caps.currentExtent.width;
		info.imageExtent.height = caps.currentExtent.height;
	}

	// format
	uint32_t formats_count;
	res = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->phdev, surface,
		&formats_count, NULL);
	if (res != VK_SUCCESS || formats_count == 0) {
		wlr_vulkan_error("failed retrieve surface formats", res);
		return NULL;
	}

	VkSurfaceFormatKHR *formats = calloc(formats_count, sizeof(*formats));
	if (!formats) {
		wlr_log(WLR_ERROR, "allocation failed");
		return NULL;
	}

	res = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan->phdev, surface,
		&formats_count, formats);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("failed retrieve surface formats", res);
		free(formats);
		return NULL;
	}

	// try to find a format matching our needs if we don't have
	// free choice
	info.imageFormat = formats[0].format;
	info.imageColorSpace = formats[0].colorSpace;
	if (formats_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
		info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	} else {
		for (unsigned i = 0; i < formats_count; ++i) {
			if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
				info.imageFormat = formats[i].format;
				info.imageColorSpace = formats[i].colorSpace;
				break;
			}
		}
	}

	// TODO: see renderpass creation in renderer.c
	// since we have to create the renderpass _before_ surfaces/swapchains
	// and use b8g8r8a8 format there, we fail if it is no available
	if (info.imageFormat != VK_FORMAT_B8G8R8A8_UNORM) {
		wlr_log(WLR_ERROR, "can't create swapchain with b8g8r8a8 format");
		free(formats);
		return NULL;
	}

	free(formats);

	// Get available present modes
	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->phdev, surface,
		&present_mode_count, NULL);

	VkPresentModeKHR *present_modes =
		calloc(present_mode_count, sizeof(VkPresentModeKHR));
	if (!present_modes) {
		wlr_log(WLR_ERROR, "allocation failed");
		return NULL;
	}

	res = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->phdev, surface,
		&present_mode_count, present_modes);
	if (res != VK_SUCCESS || present_mode_count == 0) {
		free(present_modes);
		wlr_vulkan_error("Failed to retrieve surface present modes", res);
		return NULL;
	}

	info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

	if (!vsync) {
		for (size_t i = 0; i < present_mode_count; i++) {
			if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
				info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			} else if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
	}

	free(present_modes);

	uint32_t pref_image_count = caps.minImageCount + 1;
	if ((caps.maxImageCount > 0) && (pref_image_count > caps.maxImageCount)) {
		pref_image_count = caps.maxImageCount;
	}

	// transformation
	VkSurfaceTransformFlagBitsKHR transform =
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
		transform = caps.currentTransform;
	}

	// use alpha if possible
	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkCompositeAlphaFlagBitsKHR alpha_flags[] = {
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
	};

	for (int i = 0; i < 4; ++i) {
		if (caps.supportedCompositeAlpha & alpha_flags[i]) {
			alpha = alpha_flags[i];
			break;
		}
	}

	// usage
	assert(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
		swapchain->readable = true;
		info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	} else {
		wlr_log(WLR_INFO, "Created swapchain will not be readable");
	}

	// create
	info.minImageCount = pref_image_count;
	info.preTransform = transform;
	info.imageArrayLayers = 1;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.clipped = VK_TRUE;
	info.compositeAlpha = alpha;
	swapchain->create_info = info;

	res = vkCreateSwapchainKHR(vulkan->dev, &info, NULL, &swapchain->swapchain);
	if (res != VK_SUCCESS || !swapchain->swapchain) {
		wlr_vulkan_error("Failed to create vk swapchain", res);
		return NULL;
	}

	// buffers
	if (init_swapchain_buffers(swapchain)) {
		return swapchain;
	}

	wlr_vk_swapchain_finish(swapchain);
	return NULL;
}

static bool wlr_vk_swapchain_resize(struct wlr_vk_swapchain *swapchain,
		uint32_t width, uint32_t height) {

	VkResult res;
	struct wlr_vulkan *vulkan = swapchain->renderer->vulkan;
	VkSurfaceCapabilitiesKHR caps;
	res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->phdev,
		swapchain->surface, &caps);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to retrieve surface caps", res);
		return false;
	}

	VkExtent2D ex = caps.currentExtent;
	if (ex.width == 0xFFFFFFFF && ex.height == 0xFFFFFFFF) {
		swapchain->create_info.imageExtent.width = wlr_clamp(width,
			caps.minImageExtent.width, caps.maxImageExtent.width);
		swapchain->create_info.imageExtent.height = wlr_clamp(height,
			caps.minImageExtent.height, caps.maxImageExtent.height);
	} else {
		swapchain->create_info.imageExtent = ex;
	}

	swapchain->create_info.oldSwapchain = swapchain->swapchain;
	res = vkCreateSwapchainKHR(vulkan->dev, &swapchain->create_info, NULL,
		&swapchain->swapchain);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateSwapchainKHR", res);
		return false;
	}

	if (swapchain->create_info.oldSwapchain) {
		vkDestroySwapchainKHR(vulkan->dev, swapchain->create_info.oldSwapchain,
			NULL);
		swapchain->create_info.oldSwapchain = VK_NULL_HANDLE;
	}

	if (!init_swapchain_buffers(swapchain)) {
		return false;
	}

	return true;
}

// render surface implementations
// swapchain render surface
static bool swapchain_read_pixels(struct wlr_render_surface *wlr_rs,
		enum wl_shm_format wl_fmt, uint32_t *flags,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *vdata) {
	struct wlr_vk_swapchain_render_surface *srs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	if (!srs->swapchain.readable) {
		wlr_log(WLR_ERROR, "cannot read pixels from swapchain image");
		return false;
	}

	VkImage read = srs->swapchain.buffers[srs->current_id].image;
	return vulkan_read_pixels(&srs->vk_rs, read, wl_fmt, flags,
		stride, width, height, src_x, src_y, dst_x, dst_y, vdata);
}

static bool swapchain_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->vk_rs.renderer;

	VkResult res;
	bool success = true;
	uint32_t w = rs->vk_rs.rs.width;
	uint32_t h = rs->vk_rs.rs.height;

	// track buffer age (although only used by layout changing atm)
	for (unsigned i = 0; i < rs->swapchain.image_count; ++i) {
		if (rs->swapchain.buffers[i].age > 0) {
			++rs->swapchain.buffers[i].age;
		}
	}

	rs->swapchain.buffers[rs->current_id].age = 1;

	// present
	uint32_t id = rs->current_id;
	VkPresentInfoKHR present_info = {0};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &rs->swapchain.swapchain;
	present_info.pImageIndices = &id;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &rs->present;

	VkPresentRegionsKHR present_regions = {0};
	VkPresentRegionKHR present_region = {0};

	int nrects = 0;
	pixman_box32_t *rects;
	if (damage && renderer->vulkan->extensions.incremental_present) {
		rects = pixman_region32_rectangles(damage, &nrects);
	}

	if (nrects > 0) {
		VkRectLayerKHR vk_rects[nrects];
		struct wlr_box bounds = {0, 0, w, h};

		for (int i = 0; i < nrects; ++i) {
			struct wlr_box b = {rects[i].x1, rects[i].y1,
				rects[i].x2 - rects[i].x1, rects[i].y2 - rects[i].y1};
			wlr_box_transform(&b, WL_OUTPUT_TRANSFORM_FLIPPED_180, w, h, &b);
			if (!wlr_box_intersection(&b, &bounds, &b)) {
				b = (struct wlr_box) {0, 0, 0, 0}; // empty
			}
			vk_rects[i].layer = 0;
			vk_rects[i].offset = (VkOffset2D) {b.x, b.y};
			vk_rects[i].extent = (VkExtent2D) {b.width, b.height};
		}

		present_regions.sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR;
		present_regions.swapchainCount = 1;
		present_regions.pRegions = &present_region;

		present_region.rectangleCount = nrects;
		present_region.pRectangles = vk_rects;

		present_info.pNext = &present_regions;
		res = vkQueuePresentKHR(renderer->vulkan->present_queue, &present_info);
	} else {
		res = vkQueuePresentKHR(renderer->vulkan->present_queue, &present_info);
	}

	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkQueuePresentKHR", res);
		success = false;
		goto clean;
	}

clean:
	rs->current_id = -1;
	return success;
}

static void swapchain_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	struct wlr_vulkan *vulkan = rs->vk_rs.renderer->vulkan;
	rs->vk_rs.rs.width = width;
	rs->vk_rs.rs.height = height;

	if (!wlr_vk_swapchain_resize(&rs->swapchain, width, height)) {
		wlr_log(WLR_ERROR, "Failed to resize vulkan render surface");
	}

	// check if we had an acquired image
	// if so, we have to recreate the acquire semaphore, since it may be in
	// signaled state and we can't explicitly reset that
	// althought renderer_begin -> renderer_end -> swap_buffers isn't expected
	// to be interrupted, we might have already acquired the image in an earlier
	// buffer_age call.
	if (rs->current_id != -1) {
		rs->current_id = -1;
		vkDestroySemaphore(vulkan->dev, rs->acquire, NULL);

		VkSemaphoreCreateInfo sem_info = {0};
		sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkResult res = vkCreateSemaphore(vulkan->dev, &sem_info, NULL,
			&rs->acquire);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateSemaphore", res);
		}
	}
}

static void swapchain_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	struct wlr_vk_renderer *renderer = rs->vk_rs.renderer;

	wlr_vk_swapchain_finish(&rs->swapchain);
	if (rs->surface) {
		vkDestroySurfaceKHR(renderer->vulkan->instance, rs->surface, NULL);
	}

	if (rs->vk_rs.cb) {
		vkFreeCommandBuffers(renderer->vulkan->dev, renderer->command_pool,
			1u, &rs->vk_rs.cb);
	}

	free(rs);
}

static int swapchain_render_surface_buffer_age(
		struct wlr_render_surface *wlr_rs) {
	// we can only know the buffer age once we have acquire the next image.
	// since acquiring and holding images for a long time shouldn't be
	// a problem (until we render the next time) we do it here.
	// Since we never render multiple frames in parallel this should not block.
	struct wlr_vk_swapchain_render_surface *rs =
		vulkan_get_render_surface_swapchain(wlr_rs);
	struct wlr_vulkan *vulkan = rs->vk_rs.renderer->vulkan;
	if (rs->current_id == -1) {
		VkResult res;
		uint32_t id;
		res = vkAcquireNextImageKHR(vulkan->dev, rs->swapchain.swapchain,
			0xFFFFFFFF, rs->acquire, VK_NULL_HANDLE, &id);
		if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
			wlr_vulkan_error("vkAcquireNextImageKHR", res);
			return -1;
		}

		assert(id < rs->swapchain.image_count);
		rs->current_id = id;
	}

	return rs->swapchain.buffers[rs->current_id].age;
}

static const struct wlr_render_surface_impl swapchain_render_surface_impl = {
	.buffer_age = swapchain_render_surface_buffer_age,
	.destroy = swapchain_render_surface_destroy,
	.swap_buffers = swapchain_swap_buffers,
	.resize = swapchain_render_surface_resize,
	.read_pixels = swapchain_read_pixels,
};

// offscreen render surface
static bool offscreen_read_pixels(struct wlr_render_surface *wlr_rs,
		enum wl_shm_format wl_fmt, uint32_t *flags,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *vdata) {
	struct wlr_vk_offscreen_render_surface *ors =
		vulkan_get_render_surface_offscreen(wlr_rs);
	VkImage read = ors->back->buffer.image;
	return vulkan_read_pixels(&ors->vk_rs, read, wl_fmt, flags,
		stride, width, height, src_x, src_y, dst_x, dst_y, vdata);
}

static bool offscreen_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);
	assert(rs->back);

	// we eplicitly set buffer age here since it was initialized to 0
	// only now this buffer was rendered once
	rs->back->buffer.age = 3;

	struct wlr_vk_offscreen_buffer *old_front = rs->old_front;
	if (!old_front) {
		for (unsigned i = 0; i < 3; ++i) {
			if (&rs->buffers[i] != rs->front && &rs->buffers[i] != rs->back) {
				old_front = &rs->buffers[i];
				break;
			}
		}

		assert(old_front);
	}

	rs->old_front = rs->front;
	rs->front = rs->back;
	rs->back = old_front;

	return true;
}

static void offscreen_render_surface_finish_buffers(
		struct wlr_vk_offscreen_render_surface *rs) {
	struct wlr_vulkan *vulkan = rs->vk_rs.renderer->vulkan;
	for (unsigned i = 0; i < 3; ++i) {
		struct wlr_vk_offscreen_buffer *buf = &rs->buffers[i];
		if (buf->buffer.framebuffer) {
			vkDestroyFramebuffer(vulkan->dev, buf->buffer.framebuffer, NULL);
		}
		if (buf->buffer.image_view) {
			vkDestroyImageView(vulkan->dev, buf->buffer.image_view, NULL);
		}
		if (buf->buffer.image) {
			vkDestroyImage(vulkan->dev, buf->buffer.image, NULL);
		}
		if (buf->memory) {
			vkFreeMemory(vulkan->dev, buf->memory, NULL);
		}
		if (buf->bo) {
			gbm_bo_destroy(buf->bo);
		}
	}
}

static void offscreen_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);
	offscreen_render_surface_finish_buffers(rs);
	struct wlr_vk_renderer *renderer = rs->vk_rs.renderer;
	if (rs->vk_rs.cb) {
		vkFreeCommandBuffers(renderer->vulkan->dev, renderer->command_pool,
			1u, &rs->vk_rs.cb);
	}

	free(rs);
}

static bool offscreen_render_surface_init_buffers(
		struct wlr_vk_offscreen_render_surface *rs) {

	VkResult res;
	uint32_t width = rs->vk_rs.rs.width;
	uint32_t height = rs->vk_rs.rs.height;
	struct wlr_vulkan *vulkan = rs->vk_rs.renderer->vulkan;

	// initialize buffers
	// implementation notes: we currently create buffers with
	// gbm and then import them to vulkan. Creating images with vulkan
	// and then importing them as gbm bo is probably possible as well.
	for (unsigned i = 0u; i < 3; ++i) {
		struct wlr_vk_offscreen_buffer *buf = &rs->buffers[i];

		// TODO: use queried external memory/image properties
		// (see end of init_device)
		const VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;

		VkImageCreateInfo img_info = {0};
		img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		img_info.imageType = VK_IMAGE_TYPE_2D;
		img_info.format = format;
		img_info.mipLevels = 1;
		img_info.arrayLayers = 1;
		img_info.samples = VK_SAMPLE_COUNT_1_BIT;
		img_info.tiling = VK_IMAGE_TILING_LINEAR;
		img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		img_info.extent = (VkExtent3D) { width, height, 1 };
		img_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		VkMemoryAllocateInfo mem_info = {0};
		unsigned mem_bits = 0xFFFFFFFF;

		VkExternalMemoryImageCreateInfo extimg_info = {0};
		VkImportMemoryFdInfoKHR import_info = {0};

		if (rs->gbm_dev) {
			const VkExternalMemoryHandleTypeFlagBits htype =
				VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

			// we currently need gbm_bo_use_linear to import it into
			// vulkan with linear layout. Could probably be solved
			// with the (future) vulkan modifiers extension
			buf->bo = gbm_bo_create(rs->gbm_dev,
				width, height, GBM_FORMAT_ARGB8888, rs->flags);
			if (!buf->bo) {
				wlr_log(WLR_ERROR, "Failed to create gbm_bo");
				return false;
			}

			int fd = gbm_bo_get_fd(buf->bo);
			if (fd < 0) {
				wlr_log(WLR_ERROR, "Failed to retrieve gbm_bo fd");
				return false;
			}

			// manipulate creation infos
			extimg_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
			extimg_info.handleTypes = htype;
			img_info.pNext = &extimg_info;

			struct VkMemoryFdPropertiesKHR props = {0};
			props.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
			vulkan->api.getMemoryFdPropertiesKHR(vulkan->dev, htype, fd, &props);
			mem_bits &= props.memoryTypeBits;

			import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
			import_info.fd = fd;
			import_info.handleType = htype;
			mem_info.pNext = &import_info;
		}

		res = vkCreateImage(vulkan->dev, &img_info, NULL, &buf->buffer.image);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateImage (import)", res);
			return false;
		}

		// allocate memory
		// we always use dedicated memory anyways
		VkMemoryRequirements mem_reqs = {0};
		vkGetImageMemoryRequirements(vulkan->dev, buf->buffer.image, &mem_reqs);

		// create memory
		mem_bits &= mem_reqs.memoryTypeBits;
		mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mem_info.allocationSize = mem_reqs.size;
		mem_info.memoryTypeIndex = wlr_vulkan_find_mem_type(vulkan, 0, mem_bits);

		res = vkAllocateMemory(vulkan->dev, &mem_info,
			NULL, &buf->memory);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkAllocateMemory (import)", res);
			return false;
		}

		// bind it
		res = vkBindImageMemory(vulkan->dev, buf->buffer.image,
			buf->memory, 0);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkBindMemory (imported)", res);
			return false;
		}

		VkImageViewCreateInfo view_info = {0};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
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
			return false;
		}

		VkFramebufferCreateInfo fb_info = {0};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments = &buf->buffer.image_view;
		fb_info.renderPass = rs->vk_rs.renderer->render_pass;
		fb_info.width = width;
		fb_info.height = height;
		fb_info.layers = 1;

		res = vkCreateFramebuffer(vulkan->dev, &fb_info, NULL,
			&buf->buffer.framebuffer);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkCreateFramebuffer", res);
			return false;
		}
	}

	return true;
}

static void offscreen_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);
	if (rs->vk_rs.rs.width == width && rs->vk_rs.rs.height == height) {
		return;
	}

	offscreen_render_surface_finish_buffers(rs);
	rs->vk_rs.rs.width = width;
	rs->vk_rs.rs.height = height;
	offscreen_render_surface_init_buffers(rs);
}

static int offscreen_render_surface_buffer_age(
		struct wlr_render_surface *wlr_rs) {
	// struct wlr_vk_offscreen_render_surface *rs =
	// 	vulkan_get_render_surface_offscreen(wlr_rs);
	// return rs->back->buffer.age;

	// TODO: reduces flickering
	return -1;
}

static struct gbm_bo *offscreen_render_surface_get_bo(
		struct wlr_render_surface *wlr_rs) {
	struct wlr_vk_offscreen_render_surface *rs =
		vulkan_get_render_surface_offscreen(wlr_rs);

	// TODO: we need explicit fencing to synchronise this correctly
	// this way, flickering is to be expected
	return rs->front ? rs->front->bo : NULL;
}

static const struct wlr_render_surface_impl offscreen_render_surface_impl = {
	.buffer_age = offscreen_render_surface_buffer_age,
	.destroy = offscreen_render_surface_destroy,
	.swap_buffers = offscreen_swap_buffers,
	.resize = offscreen_render_surface_resize,
	.get_bo = offscreen_render_surface_get_bo,
	.read_pixels = offscreen_read_pixels,
};

// swapchain render surface
static struct wlr_render_surface *swapchain_render_surface_create(
		struct wlr_vk_renderer *renderer, uint32_t width, uint32_t height,
		VkSurfaceKHR surface) {

	VkResult res;
	struct wlr_vk_swapchain_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->vk_rs.renderer = renderer;
	rs->vk_rs.rs.width = width;
	rs->vk_rs.rs.height = height;
	rs->surface = surface;
	rs->current_id = -1;

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
		&rs->vk_rs.cb);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		goto error;
	}

	wlr_render_surface_init(&rs->vk_rs.rs,
		&swapchain_render_surface_impl);

	if (!wlr_vk_swapchain_init(&rs->swapchain, renderer, rs->surface,
			width, height, false)) {
		wlr_log(WLR_ERROR, "Failed to initialize wlr_vk_swapchain");
		goto error;
	}

	return &rs->vk_rs.rs;

error:
	swapchain_render_surface_destroy(&rs->vk_rs.rs);
	return NULL;
}

struct wlr_render_surface *vulkan_render_surface_create_headless(
		struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height) {
	// TODO: probably best to use custom implementation for headless
	// since we don't actually need multiple buffers.

	VkResult res;
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vulkan *vulkan = renderer->vulkan;
	struct wlr_vk_offscreen_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->vk_rs.renderer = renderer;
	rs->vk_rs.rs.width = width;
	rs->vk_rs.rs.height = height;
	wlr_render_surface_init(&rs->vk_rs.rs, &offscreen_render_surface_impl);

	// command buffer
	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = 1u;
	res = vkAllocateCommandBuffers(vulkan->dev, &cmd_buf_info,
		&rs->vk_rs.cb);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		return NULL;
	}

	if (!offscreen_render_surface_init_buffers(rs)) {
		offscreen_render_surface_destroy(&rs->vk_rs.rs);
		return NULL;
	}

	rs->back = &rs->buffers[0];
	return &rs->vk_rs.rs;
}

struct wlr_render_surface *vulkan_render_surface_create_xcb(
		struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height,
		void *xcb_connection, uint32_t xcb_window) {
#ifdef WLR_HAS_X11_BACKEND
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	if (!renderer->vulkan->extensions.xcb) {
		wlr_log(WLR_ERROR, "Can't create xcb render surface since vulkan "
			"extension is not supported");
		return NULL;
	}

	VkSurfaceKHR surf;
	VkXcbSurfaceCreateInfoKHR info = {0};
	info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	info.connection = (xcb_connection_t *)xcb_connection;
	info.window = xcb_window;
	VkResult res = vkCreateXcbSurfaceKHR(renderer->vulkan->instance, &info,
		NULL, &surf);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create x11 vk surface", res);
		return NULL;
	}

	return swapchain_render_surface_create(renderer, width, height, surf);
#else
	return NULL;
#endif
}

struct wlr_render_surface *vulkan_render_surface_create_wl(
		struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height,
		struct wl_display *disp, struct wl_surface *wl_surface) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	if (!renderer->vulkan->extensions.wayland) {
		wlr_log(WLR_ERROR, "Can't create wayland render surface since vulkan "
			"extension is not supported");
		return NULL;
	}

	VkSurfaceKHR surf;
	VkWaylandSurfaceCreateInfoKHR info = {0};
	info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	info.display = disp;
	info.surface = wl_surface;
	VkResult res = vkCreateWaylandSurfaceKHR(renderer->vulkan->instance,
		&info, NULL, &surf);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("Failed to create wl vk surface", res);
		return NULL;
	}

	return swapchain_render_surface_create(renderer, width, height, surf);
}

struct wlr_render_surface *vulkan_render_surface_create_gbm(
		struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height,
		struct gbm_device *gbm_dev, uint32_t flags) {
	VkResult res;
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_vulkan *vulkan = renderer->vulkan;
	if (!vulkan->extensions.dmabuf) {
		wlr_log(WLR_ERROR, "Can't create gbm render surface since vk dmabuf "
			"extension is not supported");
		return NULL;
	}

	assert(vulkan->extensions.external_mem_fd);
	struct wlr_vk_offscreen_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	flags |= GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;

	rs->vk_rs.renderer = renderer;
	rs->vk_rs.rs.width = width;
	rs->vk_rs.rs.height = height;
	rs->flags = flags;
	rs->gbm_dev = gbm_dev;
	wlr_render_surface_init(&rs->vk_rs.rs, &offscreen_render_surface_impl);

	// command buffer
	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = 1u;
	res = vkAllocateCommandBuffers(vulkan->dev, &cmd_buf_info,
		&rs->vk_rs.cb);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkAllocateCommandBuffers", res);
		return NULL;
	}

	if (!offscreen_render_surface_init_buffers(rs)) {
		offscreen_render_surface_destroy(&rs->vk_rs.rs);
		return NULL;
	}

	rs->back = &rs->buffers[0];
	return &rs->vk_rs.rs;
}

VkFramebuffer vulkan_render_surface_begin(struct wlr_vk_render_surface *rs,
		VkCommandBuffer cb) {
	struct wlr_vk_swapchain_buffer *buffer = NULL;
	if (vulkan_render_surface_is_swapchain(&rs->rs)) {
		// calling buffer_age will implicitly acquire the next image,
		// if not already done so
		struct wlr_vk_swapchain_render_surface *srs =
			vulkan_get_render_surface_swapchain(&rs->rs);
		swapchain_render_surface_buffer_age(&rs->rs);
		if (srs->current_id < 0) { // acquiring failed
			return VK_NULL_HANDLE;
		}

		assert(srs->current_id < (int) srs->swapchain.image_count);
		buffer = &srs->swapchain.buffers[srs->current_id];
	} else {
		struct wlr_vk_offscreen_render_surface *ors =
			vulkan_get_render_surface_offscreen(&rs->rs);
		buffer = &ors->back->buffer;
	}

	if (buffer->age == 0) {
		vulkan_change_layout(cb, buffer->image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
		buffer->layout_changed = true;
	}

	return buffer->framebuffer;
}

void vulkan_render_surface_end(struct wlr_vk_render_surface *rs,
		VkSemaphore *wait, VkSemaphore *signal) {
	*wait = *signal = VK_NULL_HANDLE;

	if (vulkan_render_surface_is_swapchain(&rs->rs)) {
		struct wlr_vk_swapchain_render_surface *srs =
			vulkan_get_render_surface_swapchain(&rs->rs);
		*wait = srs->acquire;
		*signal = srs->present;
	}
}
