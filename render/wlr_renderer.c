#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/interface.h>
#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include <wlr/config.h>

#if WLR_HAS_GLES2_RENDERER
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#endif

#if WLR_HAS_VULKAN_RENDERER
#include <wlr/render/vulkan.h>
#endif // WLR_HAS_VULKAN_RENDERER

#include "util/signal.h"
#include "render/pixel_format.h"
#include "render/wlr_renderer.h"

void wlr_renderer_init(struct wlr_renderer *renderer,
		const struct wlr_renderer_impl *impl) {
	assert(impl->begin);
	assert(impl->clear);
	assert(impl->scissor);
	assert(impl->render_subtexture_with_matrix);
	assert(impl->render_quad_with_matrix);
	assert(impl->get_shm_texture_formats);
	assert(impl->get_render_buffer_caps);
	renderer->impl = impl;

	wl_signal_init(&renderer->events.destroy);
}

void wlr_renderer_destroy(struct wlr_renderer *r) {
	if (!r) {
		return;
	}

	assert(!r->rendering);

	wlr_signal_emit_safe(&r->events.destroy, r);

	if (r->impl && r->impl->destroy) {
		r->impl->destroy(r);
	} else {
		free(r);
	}
}

bool renderer_bind_buffer(struct wlr_renderer *r, struct wlr_buffer *buffer) {
	assert(!r->rendering);
	if (!r->impl->bind_buffer) {
		return false;
	}
	return r->impl->bind_buffer(r, buffer);
}

void wlr_renderer_begin(struct wlr_renderer *r, uint32_t width, uint32_t height) {
	assert(!r->rendering);

	r->impl->begin(r, width, height);

	r->rendering = true;
}

bool wlr_renderer_begin_with_buffer(struct wlr_renderer *r,
		struct wlr_buffer *buffer) {
	if (!renderer_bind_buffer(r, buffer)) {
		return false;
	}
	wlr_renderer_begin(r, buffer->width, buffer->height);
	r->rendering_with_buffer = true;
	return true;
}

void wlr_renderer_end(struct wlr_renderer *r) {
	assert(r->rendering);

	if (r->impl->end) {
		r->impl->end(r);
	}

	r->rendering = false;

	if (r->rendering_with_buffer) {
		renderer_bind_buffer(r, NULL);
		r->rendering_with_buffer = false;
	}
}

void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]) {
	assert(r->rendering);
	r->impl->clear(r, color);
}

void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box) {
	assert(r->rendering);
	r->impl->scissor(r, box);
}

bool wlr_render_texture(struct wlr_renderer *r, struct wlr_texture *texture,
		const float projection[static 9], int x, int y, float alpha) {
	struct wlr_box box = {
		.x = x,
		.y = y,
		.width = texture->width,
		.height = texture->height,
	};

	float matrix[9];
	wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	return wlr_render_texture_with_matrix(r, texture, matrix, alpha);
}

bool wlr_render_texture_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const float matrix[static 9],
		float alpha) {
	struct wlr_fbox box = {
		.x = 0,
		.y = 0,
		.width = texture->width,
		.height = texture->height,
	};
	return wlr_render_subtexture_with_matrix(r, texture, &box, matrix, alpha);
}

bool wlr_render_subtexture_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const struct wlr_fbox *box,
		const float matrix[static 9], float alpha) {
	assert(r->rendering);
	return r->impl->render_subtexture_with_matrix(r, texture,
		box, matrix, alpha);
}

void wlr_render_rect(struct wlr_renderer *r, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	if (box->width == 0 || box->height == 0) {
		return;
	}
	assert(box->width > 0 && box->height > 0);
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	wlr_render_quad_with_matrix(r, color, matrix);
}

void wlr_render_quad_with_matrix(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	assert(r->rendering);
	r->impl->render_quad_with_matrix(r, color, matrix);
}

const uint32_t *wlr_renderer_get_shm_texture_formats(struct wlr_renderer *r,
		size_t *len) {
	return r->impl->get_shm_texture_formats(r, len);
}

const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_texture_formats(
		struct wlr_renderer *r) {
	if (!r->impl->get_dmabuf_texture_formats) {
		return NULL;
	}
	return r->impl->get_dmabuf_texture_formats(r);
}

const struct wlr_drm_format_set *wlr_renderer_get_render_formats(
		struct wlr_renderer *r) {
	if (!r->impl->get_render_formats) {
		return NULL;
	}
	return r->impl->get_render_formats(r);
}

uint32_t renderer_get_render_buffer_caps(struct wlr_renderer *r) {
	return r->impl->get_render_buffer_caps(r);
}

bool wlr_renderer_read_pixels(struct wlr_renderer *r, uint32_t fmt,
		uint32_t *flags, uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		void *data) {
	if (!r->impl->read_pixels) {
		return false;
	}
	return r->impl->read_pixels(r, fmt, flags, stride, width, height,
		src_x, src_y, dst_x, dst_y, data);
}

bool wlr_renderer_init_wl_display(struct wlr_renderer *r,
		struct wl_display *wl_display) {
	if (wl_display_init_shm(wl_display)) {
		wlr_log(WLR_ERROR, "Failed to initialize shm");
		return false;
	}

	size_t len;
	const uint32_t *formats = wlr_renderer_get_shm_texture_formats(r, &len);
	if (formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to initialize shm: cannot get formats");
		return false;
	}

	bool argb8888 = false, xrgb8888 = false;
	for (size_t i = 0; i < len; ++i) {
		// ARGB8888 and XRGB8888 must be supported and are implicitly
		// advertised by wl_display_init_shm
		enum wl_shm_format fmt = convert_drm_format_to_wl_shm(formats[i]);
		switch (fmt) {
		case WL_SHM_FORMAT_ARGB8888:
			argb8888 = true;
			break;
		case WL_SHM_FORMAT_XRGB8888:
			xrgb8888 = true;
			break;
		default:
			wl_display_add_shm_format(wl_display, fmt);
		}
	}
	assert(argb8888 && xrgb8888);

	if (wlr_renderer_get_dmabuf_texture_formats(r) != NULL) {
		if (wlr_renderer_get_drm_fd(r) >= 0) {
			if (wlr_drm_create(wl_display, r) == NULL) {
				return false;
			}
		} else {
			wlr_log(WLR_INFO, "Cannot get renderer DRM FD, disabling wl_drm");
		}

		if (wlr_linux_dmabuf_v1_create(wl_display, r) == NULL) {
			return false;
		}
	}

	return true;
}

struct wlr_renderer *renderer_autocreate_with_drm_fd(int drm_fd) {
	const char *name = getenv("WLR_RENDERER");
	if (name) {
		wlr_log(WLR_INFO, "Loading user-specified renderer due to WLR_RENDERER: %s",
			name);

#if WLR_HAS_GLES2_RENDERER
		if (strcmp(name, "gles2") == 0) {
			if (drm_fd < 0) {
				wlr_log(WLR_ERROR, "Cannot create GLES2 renderer: "
					"no DRM FD available");
				return NULL;
			}
			return wlr_gles2_renderer_create_with_drm_fd(drm_fd);
		}
#endif
#if WLR_HAS_VULKAN_RENDERER
		if (strcmp(name, "vulkan") == 0) {
			return wlr_vk_renderer_create_with_drm_fd(drm_fd);
		}
#endif
		if (strcmp(name, "pixman") == 0) {
			return wlr_pixman_renderer_create();
		}

		wlr_log(WLR_ERROR, "Invalid WLR_RENDERER value: '%s'", name);
		return NULL;
	}

	struct wlr_renderer *renderer = NULL;
#if WLR_HAS_GLES2_RENDERER
	if (drm_fd >= 0) {
		if ((renderer = wlr_gles2_renderer_create_with_drm_fd(drm_fd)) != NULL) {
			return renderer;
		}
		wlr_log(WLR_DEBUG, "Failed to create GLES2 renderer");
	} else {
		wlr_log(WLR_DEBUG, "Skipping GLES2 renderer: no DRM FD available");
	}
#endif

	if ((renderer = wlr_pixman_renderer_create()) != NULL) {
		return renderer;
	}
	wlr_log(WLR_DEBUG, "Failed to create pixman renderer");

	wlr_log(WLR_ERROR, "Could not initialize renderer");
	return NULL;
}

struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend) {
	// Note, drm_fd may be negative if unavailable
	int drm_fd = wlr_backend_get_drm_fd(backend);
	return renderer_autocreate_with_drm_fd(drm_fd);
}

int wlr_renderer_get_drm_fd(struct wlr_renderer *r) {
	if (!r->impl->get_drm_fd) {
		return -1;
	}
	return r->impl->get_drm_fd(r);
}

bool wlr_renderer_wait_timeline(struct wlr_renderer *r,
		struct wlr_render_timeline *timeline, uint64_t src_point) {
	if (!r->impl->wait_timeline) {
		return false;
	}
	return r->impl->wait_timeline(r, timeline, src_point);
}

bool wlr_renderer_signal_timeline(struct wlr_renderer *r,
		struct wlr_render_timeline *timeline, uint64_t dst_point) {
	if (!r->impl->signal_timeline) {
		return false;
	}
	return r->impl->signal_timeline(r, timeline, dst_point);
}
