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
#include <wlr/backend/interface.h>
#include <render/vulkan.h>

#include "shaders/common.vert.h"
#include "shaders/texture.frag.h"
#include "shaders/quad.frag.h"
#include "shaders/ellipse.frag.h"

// host_images: Internal configuration, mainly for testing and benchmarking.
// Should be false in release builds.
// When this is set to true, will use linear textures on host visible
// memory for shm textures.
// This means that no additional staging buffer
// upload logic (and memory for those upload buffers) has to be used
// and that uploads (e.g. write_pixels) may be faster but rendering
// may be considerably slower. Even if set to true, the device
// may not support reading from linear textures (see checks below).
static const bool use_host_images = false;

static const VkDeviceSize min_stage_size = 1024 * 1024; // 1MB
static const VkDeviceSize max_stage_size = 64 * min_stage_size; // 64MB
static const size_t start_descriptor_pool_size = 256u;
static bool default_debug = true;

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

struct wlr_vk_descriptor_pool *wlr_vk_alloc_texture_ds(
		struct wlr_vk_renderer *renderer, VkDescriptorSet *ds) {
	VkResult res;
	VkDescriptorSetAllocateInfo ds_info = {0};
	ds_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ds_info.descriptorSetCount = 1;
	ds_info.pSetLayouts = &renderer->descriptor_set_layout;

	bool found = false;
	struct wlr_vk_descriptor_pool *pool;
	wl_list_for_each(pool, &renderer->descriptor_pools, link) {
		if (pool->free > 0) {
			found = true;
			break;
		}
	}

	if (!found) { // create new pool
		pool = calloc(1, sizeof(*pool));
		if (!pool) {
			wlr_log_errno(WLR_ERROR, "allocation failed");
			return NULL;
		}

		size_t count = renderer->last_pool_size;
		if (!count) {
			count = start_descriptor_pool_size;
		}

		pool->free = count;
		VkDescriptorPoolSize pool_size = {0};
		pool_size.descriptorCount = count;
		pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		VkDescriptorPoolCreateInfo dpool_info = {0};
		dpool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		dpool_info.maxSets = count;
		dpool_info.poolSizeCount = 1;
		dpool_info.pPoolSizes = &pool_size;
		dpool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

		res = vkCreateDescriptorPool(renderer->dev->dev, &dpool_info, NULL,
			&pool->pool);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkCreateDescriptorPool", res);
			free(pool);
			return NULL;
		}

		wl_list_insert(&renderer->descriptor_pools, &pool->link);
	}

	ds_info.descriptorPool = pool->pool;
	res = vkAllocateDescriptorSets(renderer->dev->dev, &ds_info, ds);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocateDescriptorSets", res);
		return NULL;
	}

	return pool;
}

void wlr_vk_free_ds(struct wlr_vk_renderer *renderer,
		struct wlr_vk_descriptor_pool *pool, VkDescriptorSet ds) {
	vkFreeDescriptorSets(renderer->dev->dev, pool->pool, 1, &ds);
	++pool->free;
}

static void shared_buffer_destroy(struct wlr_vk_renderer *r,
		struct wlr_vk_shared_buffer *buffer) {
	if (!buffer) {
		return;
	}

	if (buffer->allocs_size > 0) {
		wlr_log(WLR_ERROR, "shared_buffer_finish: %d allocations left",
			(unsigned) buffer->allocs_size);
	}

	free(buffer->allocs);
	if (buffer->buffer) {
		vkDestroyBuffer(r->dev->dev, buffer->buffer, NULL);
	}
	if (buffer->memory) {
		vkFreeMemory(r->dev->dev, buffer->memory, NULL);
	}

	wl_list_remove(&buffer->link);
	free(buffer);
}

static void release_stage_allocations(struct wlr_vk_renderer *renderer) {
	struct wlr_vk_shared_buffer *buf;
	wl_list_for_each(buf, &renderer->stage.buffers, link) {
		buf->allocs_size = 0u;
	}
}

struct wlr_vk_buffer_span wlr_vk_get_stage_span(struct wlr_vk_renderer *r,
		VkDeviceSize size) {
	// try to find free span
	// simple greedy allocation algorithm - should be enough for this usecase
	// since all allocations are freed together after the frame
	struct wlr_vk_shared_buffer *buf;
	wl_list_for_each_reverse(buf, &r->stage.buffers, link) {
		VkDeviceSize start = 0u;
		if (buf->allocs_size > 0) {
			struct wlr_vk_allocation *last = &buf->allocs[buf->allocs_size - 1];
			start = last->start + last->size;
		}

		assert(start <= buf->buf_size);
		if (buf->buf_size - start < size) {
			continue;
		}

		++buf->allocs_size;
		if (buf->allocs_size > buf->allocs_capacity) {
			buf->allocs_capacity = buf->allocs_size * 2;
			void *allocs = realloc(buf->allocs,
				buf->allocs_capacity * sizeof(*buf->allocs));
			if (!allocs) {
				wlr_log_errno(WLR_ERROR, "Allocation failed");
				goto error_alloc;
			}

			buf->allocs = allocs;
		}

		struct wlr_vk_allocation *a = &buf->allocs[buf->allocs_size - 1];
		a->start = start;
		a->size = size;
		return (struct wlr_vk_buffer_span) {
			.buffer = buf,
			.alloc = *a,
		};
	}

	// we didn't find a free buffer - create one
	// size = clamp(max(size * 2, prev_size * 2), min_size, max_size)
	VkDeviceSize bsize = size * 2;
	bsize = bsize < min_stage_size ? min_stage_size : bsize;
	if (!wl_list_empty(&r->stage.buffers)) {
		struct wl_list *last_link = r->stage.buffers.prev;
		struct wlr_vk_shared_buffer *prev = wl_container_of(
			last_link, prev, link);
		VkDeviceSize last_size = 2 * prev->buf_size;
		bsize = bsize < last_size ? last_size : bsize;
	}

	if (bsize > max_stage_size) {
		wlr_log(WLR_INFO, "vulkan stage buffers have reached max size");
		bsize = max_stage_size;
	}

	// create buffer
	buf = calloc(1, sizeof(*buf));
	if (!buf) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error_alloc;
	}

	VkResult res;
	VkBufferCreateInfo buf_info = {0};
	buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buf_info.size = bsize;
	buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	res = vkCreateBuffer(r->dev->dev, &buf_info, NULL, &buf->buffer);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateBuffer", res);
		goto error;
	}

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(r->dev->dev, buf->buffer, &mem_reqs);

	VkMemoryAllocateInfo mem_info = {0};
	mem_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_info.allocationSize = mem_reqs.size;
	mem_info.memoryTypeIndex = wlr_vk_find_mem_type(r->dev,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mem_reqs.memoryTypeBits);
	res = vkAllocateMemory(r->dev->dev, &mem_info, NULL, &buf->memory);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocatorMemory", res);
		goto error;
	}

	res = vkBindBufferMemory(r->dev->dev, buf->buffer, buf->memory, 0);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkBindBufferMemory", res);
		goto error;
	}

	size_t start_count = 8u;
	buf->allocs = calloc(start_count, sizeof(*buf->allocs));
	if (!buf->allocs) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error;
	}

	wlr_log(WLR_DEBUG, "Created new vk staging buffer of size %lu", bsize);
	buf->buf_size = bsize;
	wl_list_insert(&r->stage.buffers, &buf->link);

	buf->allocs_capacity = start_count;
	buf->allocs_size = 1u;
	buf->allocs[0].start = 0u;
	buf->allocs[0].size = size;
	return (struct wlr_vk_buffer_span) {
		.buffer = buf,
		.alloc = buf->allocs[0],
	};

error:
	shared_buffer_destroy(r, buf);

error_alloc:
	return (struct wlr_vk_buffer_span) {
		.buffer = NULL,
		.alloc = (struct wlr_vk_allocation) {0, 0},
	};
}

VkCommandBuffer wlr_vk_record_stage_cb(struct wlr_vk_renderer *renderer) {
	if (!renderer->stage.recording) {
		VkCommandBufferBeginInfo begin_info = {0};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(renderer->stage.cb, &begin_info);
		renderer->stage.recording = true;
	}

	return renderer->stage.cb;
}

bool wlr_vk_submit_stage_wait(struct wlr_vk_renderer *renderer) {
	if (!renderer->stage.recording) {
		return false;
	}

	vkEndCommandBuffer(renderer->stage.cb);
	renderer->stage.recording = false;

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1u;
	submit_info.pCommandBuffers = &renderer->stage.cb;
	VkResult res = vkQueueSubmit(renderer->graphics_queue.queue, 1,
		&submit_info, renderer->fence);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkQueueSubmit", res);
		return false;
	}

	res = vkWaitForFences(renderer->dev->dev, 1, &renderer->fence, true,
		UINT64_MAX);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkWaitForFences", res);
		return false;
	}

	// NOTE: don't release stage allocations here since they may still be
	// used for reading. Will be done next frame.
	res = vkResetFences(renderer->dev->dev, 1, &renderer->fence);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkResetFences", res);
		return false;
	}

	return true;
}

struct wlr_vk_pixel_format_props *wlr_vk_format_from_wl(
		struct wlr_vk_renderer *renderer, enum wl_shm_format wl_fmt) {
	for (size_t i = 0; i < renderer->format_count; ++i) {
		if (renderer->formats[i].format.wl_format == wl_fmt) {
			return &renderer->formats[i];
		}
	}
	return NULL;
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
	VkResult res;

	vkCmdEndRenderPass(cb);
	vkEndCommandBuffer(cb);

	// render semaphores
	unsigned rwait_count = 0u;
	unsigned rsignal_count = 0u;
	VkSemaphore rwait[2] = {renderer->stage.signal, VK_NULL_HANDLE};
	VkSemaphore rsignal[1] = {0};
	VkShaderStageFlags rwaitStages[2] = {0};

	unsigned submit_count = 0u;
	VkSubmitInfo submit_infos[2] = {0};

	vulkan_render_surface_end(renderer->current, &rwait[0], &rsignal[0]);
	if (rwait[0]) {
		rwaitStages[rwait_count] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		++rwait_count;
	}
	if (rsignal[0]) {
		++rsignal_count;
	}

	if (renderer->stage.recording) {
		vkEndCommandBuffer(renderer->stage.cb);
		renderer->stage.recording = false;;

		VkSubmitInfo *stage_sub = &submit_infos[submit_count];
		stage_sub->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		stage_sub->commandBufferCount = 1u;
		stage_sub->pCommandBuffers = &renderer->stage.cb;
		stage_sub->signalSemaphoreCount = 1;
		stage_sub->pSignalSemaphores = &renderer->stage.signal;
		++submit_count;

		rwait[rwait_count] = renderer->stage.signal;
		rwaitStages[rwait_count] = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		++rwait_count;
	}

	VkSubmitInfo *render_sub = &submit_infos[submit_count];
	render_sub->sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	render_sub->pCommandBuffers = &cb;
	render_sub->commandBufferCount = 1u;
	render_sub->waitSemaphoreCount = rwait_count;
	render_sub->pWaitSemaphores = rwait;
	render_sub->pWaitDstStageMask = rwaitStages;
	render_sub->signalSemaphoreCount = rsignal_count;
	render_sub->pSignalSemaphores = rsignal;
	++submit_count;

	res = vkQueueSubmit(renderer->graphics_queue.queue, submit_count,
		submit_infos, renderer->fence);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkQueueSubmit", res);
		goto clean;
	}

	// sadly this is required due to the current api/rendering model of wlr
	// ideally we could use gpu and cpu in parallel (_without_ the
	// implicit synchronization overhead and mess of opengl drivers)
	res = vkWaitForFences(renderer->dev->dev, 1, &renderer->fence, true,
		UINT64_MAX);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkWaitForFences", res);
		goto clean;
	}

	++renderer->stage.frame;
	release_stage_allocations(renderer);
	res = vkResetFences(renderer->dev->dev, 1, &renderer->fence);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkResetFences", res);
		goto clean;
	}

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
	struct wlr_box dst = {0, 0, w, h};
	if (box && !wlr_box_intersection(&dst, box, &dst)) {
		dst = (struct wlr_box) {0, 0, 0, 0}; // empty
	}

	VkRect2D rect = (VkRect2D) {{dst.x, dst.y}, {dst.width, dst.height}};
	renderer->scissor = rect;
	vkCmdSetScissor(cb, 0, 1, &rect);
}

static bool vulkan_resource_is_wl_drm_buffer(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource) {
	return false;
}

const enum wl_shm_format *vulkan_formats(struct wlr_renderer *wlr_renderer,
		size_t *len) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	*len = renderer->format_count;
	return renderer->wl_formats;
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
	wlr_log(WLR_ERROR, "vulkan dmabuf not implemented");
	return -1;
}

static int vulkan_get_dmabuf_modifiers(struct wlr_renderer *wlr_renderer,
		int format, uint64_t **modifiers) {
	wlr_log(WLR_ERROR, "vulkan dmabuf not implemented");
	return -1;
}

static bool vulkan_format_supported(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	return wlr_vk_format_from_wl(renderer, wl_fmt);
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
	struct wlr_vk_device *dev = renderer->dev;
	if (!dev) {
		free(renderer);
		return;
	}

	// stage.cb automatically freed with command pool
	struct wlr_vk_shared_buffer *buf, *tmp_buf;
	wl_list_for_each_safe(buf, tmp_buf, &renderer->stage.buffers, link) {
		shared_buffer_destroy(renderer, buf);
	}

	struct wlr_vk_descriptor_pool *pool, *tmp_pool;
	wl_list_for_each_safe(pool, tmp_pool, &renderer->descriptor_pools, link) {
		vkDestroyDescriptorPool(dev->dev, pool->pool, NULL);
		free(pool);
	}

	if (renderer->stage.signal) {
		vkDestroySemaphore(dev->dev, renderer->stage.signal, NULL);
	}
	if (renderer->fence) {
		vkDestroyFence(dev->dev, renderer->fence, NULL);
	}
	if (renderer->tex_pipe) {
		vkDestroyPipeline(dev->dev, renderer->tex_pipe, NULL);
	}
	if (renderer->quad_pipe) {
		vkDestroyPipeline(dev->dev, renderer->quad_pipe, NULL);
	}
	if (renderer->ellipse_pipe) {
		vkDestroyPipeline(dev->dev, renderer->ellipse_pipe, NULL);
	}
	if (renderer->pipeline_layout) {
		vkDestroyPipelineLayout(dev->dev, renderer->pipeline_layout, NULL);
	}
	if (renderer->descriptor_set_layout) {
		vkDestroyDescriptorSetLayout(dev->dev,
			renderer->descriptor_set_layout, NULL);
	}
	if (renderer->sampler) {
		vkDestroySampler(dev->dev, renderer->sampler, NULL);
	}
	if (renderer->render_pass) {
		vkDestroyRenderPass(dev->dev, renderer->render_pass, NULL);
	}
	if (renderer->command_pool) {
		vkDestroyCommandPool(dev->dev, renderer->command_pool, NULL);
	}

	struct wlr_vk_instance *ini = dev->instance;
	if (--dev->renderers == 0) {
		wlr_vk_device_destroy(dev);
	}

	if (wl_list_empty(&ini->devices)) {
		wlr_vk_instance_destroy(ini);
	}
	free(renderer);
}

static void vulkan_init_wl_display(struct wlr_renderer *wlr_renderer,
		struct wl_display *wl_display) {
	// probably have to implement wl_drm ourselves since mesa
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
static bool init_pipelines(struct wlr_vk_renderer *renderer, VkFormat format) {
	// util
	VkDevice dev = renderer->dev->dev;
	VkResult res;

	// renderpass
	VkAttachmentDescription attachment = {0};
	attachment.format = format;
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
		wlr_vk_error("Failed to create render pass", res);
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
		wlr_vk_error("Failed to create sampler", res);
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
		wlr_vk_error("Failed to create pipeline layout", res);
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
		wlr_vk_error("Failed to create vertex shader module", res);
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
		wlr_vk_error("Failed to create tex fragment shader module", res);
		goto cleanup_shaders;
	}

	// quad frag
	sinfo.codeSize = sizeof(quad_frag_data);
	sinfo.pCode = quad_frag_data;
	res = vkCreateShaderModule(dev, &sinfo, NULL, &quad_frag_module);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Failed to create quad fragment shader module", res);
		goto cleanup_shaders;
	}

	// ellipse frag
	sinfo.codeSize = sizeof(ellipse_frag_data);
	sinfo.pCode = ellipse_frag_data;
	res = vkCreateShaderModule(dev, &sinfo, NULL, &ellipse_frag_module);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Failed to create ellipse fragment shader module", res);
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

struct wlr_renderer *wlr_vk_renderer_create_for_device(
		struct wlr_vk_device *dev, const struct wlr_vk_queue *graphics,
		const struct wlr_vk_queue *present) {
	struct wlr_vk_renderer *renderer;
	VkResult res;
	if (!(renderer = calloc(1, sizeof(*renderer)))) {
		wlr_log_errno(WLR_ERROR, "failed to allocate wlr_vk_renderer");
		return NULL;
	}

	++dev->renderers;
	renderer->dev = dev;
	renderer->present_queue = *present;
	renderer->graphics_queue = *graphics;
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	wl_list_init(&renderer->stage.buffers);
	wl_list_init(&renderer->descriptor_pools);

	// - check device format support -
	renderer->host_images = use_host_images;
	VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkFormatFeatureFlags req_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if (renderer->host_images) {
		tiling = VK_IMAGE_TILING_LINEAR;
	} else {
		usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		req_features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	}

	size_t max_fmts;
	const struct wlr_vk_pixel_format *fmts = vulkan_get_format_list(&max_fmts);
	renderer->wl_formats = calloc(max_fmts, sizeof(*renderer->wl_formats));
	renderer->formats = calloc(max_fmts, sizeof(*renderer->formats));
	if (!renderer->wl_formats || !renderer->formats) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		goto error;
	}

	VkFormatProperties props;
	for (unsigned i = 0u; i < max_fmts; ++i) {
		struct wlr_vk_pixel_format_props fprops;
		fprops.format = fmts[i];

		// format properties
		vkGetPhysicalDeviceFormatProperties(dev->phdev,
			fmts[i].vk_format, &props);
		VkFormatFeatureFlags features = props.optimalTilingFeatures;
		if (renderer->host_images) {
			features = props.linearTilingFeatures;
		}

		if ((features & req_features) != req_features) {
			continue;
		}

		if (!wlr_vk_query_format(dev, &fprops, usage, tiling)) {
			continue;
		}

		renderer->formats[renderer->format_count] = fprops;
		renderer->wl_formats[renderer->format_count] = fmts[i].wl_format;
		++renderer->format_count;
	}

	// we currently force bgra format on swapchain creation
	// this could also be deferred until the first render surface is created.
	if (!init_pipelines(renderer, VK_FORMAT_B8G8R8A8_UNORM)) {
		goto error;
	}

	// command pool
	VkCommandPoolCreateInfo cpool_info = {0};
	cpool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cpool_info.queueFamilyIndex = graphics->family;
	res = vkCreateCommandPool(dev->dev, &cpool_info, NULL,
		&renderer->command_pool);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateCommandPool", res);
		goto error;
	}

	VkFenceCreateInfo fence_info = {0};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	res = vkCreateFence(dev->dev, &fence_info, NULL,
		&renderer->fence);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateFence", res);
		goto error;
	}

	// staging command buffer
	VkCommandBufferAllocateInfo cmd_buf_info = {0};
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = renderer->command_pool;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_buf_info.commandBufferCount = 1u;
	res = vkAllocateCommandBuffers(dev->dev, &cmd_buf_info,
		&renderer->stage.cb);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocateCommandBuffers", res);
		goto error;
	}

	VkSemaphoreCreateInfo sem_info = {0};
	sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	res = vkCreateSemaphore(dev->dev, &sem_info, NULL,
		&renderer->stage.signal);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateSemaphore", res);
		goto error;
	}

	return &renderer->wlr_renderer;

error:
	vulkan_destroy(&renderer->wlr_renderer);
	return NULL;
}

struct wlr_renderer *wlr_vk_renderer_create(struct wlr_backend *backend) {
	VkResult res;
	wlr_log(WLR_ERROR, "The vulkan renderer is only experimental and "
		"not expected to be ready for daliy use");

	if (!backend->impl->vulkan_queue_family_present_support) {
		wlr_log(WLR_ERROR, "backend does not support vulkan");
		return NULL;
	}

	// NOTE: we could add functionality to allow the compositor passing its
	// name and version to this function. Just use dummies until then,
	// shouldn't be relevant to the driver anyways
	struct wlr_vk_instance *ini = wlr_vk_instance_create(0, NULL,
		default_debug, "wlroots-compositor", 0);
	if (!ini) {
		wlr_log(WLR_ERROR, "creating vulkan instance for renderer failed");
		return NULL;
	}

	// TODO: don't just choose the first device
	// query one based on
	//  - user preference (env variables/config)
	//  - supported presenting caps (present qfam found?)
	//  - supported extensions, external memory import properties
	//  - (as default, without user preference) integrated vs dedicated
	//  - when on drm backend match it with the gbm device (probably not
	//    possible without new vulkan extensions)
	uint32_t num_devs = 1;
	VkPhysicalDevice phdev;
	res = vkEnumeratePhysicalDevices(ini->instance, &num_devs,
		&phdev);
	if ((res != VK_SUCCESS && res != VK_INCOMPLETE) || !phdev) {
		wlr_log(WLR_ERROR, "Could not retrieve physical device");
		free(ini);
		return NULL;
	}

	// queue families
	uint32_t qfam_count;
	vkGetPhysicalDeviceQueueFamilyProperties(phdev, &qfam_count, NULL);
	VkQueueFamilyProperties queue_props[qfam_count];
	vkGetPhysicalDeviceQueueFamilyProperties(phdev, &qfam_count,
		queue_props);

	unsigned queue_count = 0u;
	struct wlr_vk_queue queues[2]; // 0: present, [1: graphics if needed]
	for (unsigned i = 0u; i < qfam_count; ++i) {
		bool graphics = queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
		bool present = backend->impl->vulkan_queue_family_present_support(
			backend, ini, (uintptr_t) phdev, i);
		if (present) {
			queue_count = 2u;
			queues[0].family = i;
			if (graphics) { // one queue for present & graphics
				queue_count = 1u;
				break;
			}
		} else if (graphics) {
			graphics = true;
			queues[1].family = i;
		}
	}

	if (!queue_count) {
		wlr_log(WLR_ERROR, "unable to find present queue family for backend");
		free(ini);
		return NULL;
	}

	struct wlr_vk_device *dev = wlr_vk_device_create(ini, phdev,
		0, NULL, queue_count, queues);
	if (!dev) {
		wlr_log(WLR_ERROR, "Failed to create vulkan device");
		free(ini);
		return NULL;
	}

	return wlr_vk_renderer_create_for_device(dev, &queues[0],
		&queues[queue_count - 1]);
}
