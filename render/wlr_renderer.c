#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <gbm.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "render/shm_format.h"
#include "render/wlr_renderer.h"
#include "backend/backend.h"
#include "backend/drm/drm.h"

void wlr_renderer_init(struct wlr_renderer *renderer,
		const struct wlr_renderer_impl *impl) {
	assert(impl->begin);
	assert(impl->clear);
	assert(impl->scissor);
	assert(impl->render_subtexture_with_matrix);
	assert(impl->render_quad_with_matrix);
	assert(impl->render_ellipse_with_matrix);
	assert(impl->get_shm_texture_formats);
	assert(impl->texture_from_pixels);
	renderer->impl = impl;

	wl_signal_init(&renderer->events.destroy);
}

void wlr_renderer_destroy(struct wlr_renderer *r) {
	if (!r) {
		return;
	}
	wlr_signal_emit_safe(&r->events.destroy, r);

	if (r->impl && r->impl->destroy) {
		r->impl->destroy(r);
	} else {
		free(r);
	}
}

bool wlr_renderer_bind_buffer(struct wlr_renderer *r,
		struct wlr_buffer *buffer) {
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

void wlr_renderer_end(struct wlr_renderer *r) {
	assert(r->rendering);

	if (r->impl->end) {
		r->impl->end(r);
	}

	r->rendering = false;
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

void wlr_render_ellipse(struct wlr_renderer *r, const struct wlr_box *box,
		const float color[static 4], const float projection[static 9]) {
	if (box->width == 0 || box->height == 0) {
		return;
	}
	assert(box->width > 0 && box->height > 0);
	float matrix[9];
	wlr_matrix_project_box(matrix, box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
		projection);

	wlr_render_ellipse_with_matrix(r, color, matrix);
}

void wlr_render_ellipse_with_matrix(struct wlr_renderer *r,
		const float color[static 4], const float matrix[static 9]) {
	assert(r->rendering);
	r->impl->render_ellipse_with_matrix(r, color, matrix);
}

const uint32_t *wlr_renderer_get_shm_texture_formats(struct wlr_renderer *r,
		size_t *len) {
	return r->impl->get_shm_texture_formats(r, len);
}

bool wlr_renderer_resource_is_wl_drm_buffer(struct wlr_renderer *r,
		struct wl_resource *resource) {
	if (!r->impl->resource_is_wl_drm_buffer) {
		return false;
	}
	return r->impl->resource_is_wl_drm_buffer(r, resource);
}

void wlr_renderer_wl_drm_buffer_get_size(struct wlr_renderer *r,
		struct wl_resource *buffer, int *width, int *height) {
	if (!r->impl->wl_drm_buffer_get_size) {
		return;
	}
	return r->impl->wl_drm_buffer_get_size(r, buffer, width, height);
}

const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_texture_formats(
		struct wlr_renderer *r) {
	if (!r->impl->get_dmabuf_texture_formats) {
		return NULL;
	}
	return r->impl->get_dmabuf_texture_formats(r);
}

const struct wlr_drm_format_set *wlr_renderer_get_dmabuf_render_formats(
		struct wlr_renderer *r) {
	if (!r->impl->get_dmabuf_render_formats) {
		return NULL;
	}
	return r->impl->get_dmabuf_render_formats(r);
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

bool wlr_renderer_blit_dmabuf(struct wlr_renderer *r,
		struct wlr_dmabuf_attributes *dst,
		struct wlr_dmabuf_attributes *src) {
	assert(!r->rendering);
	if (!r->impl->blit_dmabuf) {
		return false;
	}
	return r->impl->blit_dmabuf(r, dst, src);
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

	if (r->impl->init_wl_display) {
		if (!r->impl->init_wl_display(r, wl_display)) {
			return false;
		}
	}

	return true;
}

struct wlr_renderer *wlr_renderer_autocreate_with_drm_fd(int drm_fd) {
	bool is_eglstreams = drm_is_eglstreams(drm_fd);
	struct wlr_egl *egl = NULL; 
	if (is_eglstreams)
	{
		egl = wlr_egl_create(EGL_PLATFORM_DEVICE_EXT, (void *)(long)drm_fd);
		egl->gbm_device = NULL;
		if (egl == NULL) {
			wlr_log(WLR_ERROR, "Can't initialize EGL for EGL_PLATFORM_DEVICE_EXT");
			return NULL;
		}
	} else {
		struct gbm_device *gbm_device = gbm_create_device(drm_fd);
		if (!gbm_device) {
			wlr_log(WLR_ERROR, "Failed to create GBM device");
			return NULL;
		}
	
		egl = wlr_egl_create(EGL_PLATFORM_GBM_KHR, gbm_device);
		if (egl == NULL) {
			wlr_log(WLR_ERROR, "Could not initialize EGL");
			gbm_device_destroy(gbm_device);
			return NULL;
		}
	
		egl->gbm_device = gbm_device;
	}

	struct wlr_renderer *renderer = wlr_gles2_renderer_create(egl);
	if (!renderer) {
		wlr_log(WLR_ERROR, "Failed to create GLES2 renderer");
		wlr_egl_destroy(egl);
	}

	return renderer;
}

struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend) {
	int drm_fd = backend_get_drm_fd(backend);
	if (drm_fd < 0) {
		wlr_log(WLR_ERROR, "Failed to get DRM FD from backend");
		return NULL;
	}

	return wlr_renderer_autocreate_with_drm_fd(drm_fd);
}

int wlr_renderer_get_drm_fd(struct wlr_renderer *r) {
	if (!r->impl->get_drm_fd) {
		return -1;
	}
	return r->impl->get_drm_fd(r);
}

struct wlr_egl *wlr_renderer_get_egl(struct wlr_renderer *r) {
	if (!r->impl->get_egl) {
		return NULL;
	}
	return r->impl->get_egl(r);
} 

bool wlr_renderer_wl_buffer_get_params(struct wlr_renderer *r,
	struct wl_resource *buffer, int *width, int *height, int *inverted_y) {
	assert(wlr_resource_is_buffer(buffer));

	struct wlr_egl *egl = wlr_renderer_get_egl(r);
	if (!egl) {
		return false;
	}

	if (width && egl->procs.eglQueryWaylandBufferWL(egl->display,
			buffer, EGL_WIDTH, width) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "Failed to get resource width");
		return false;
	}
	if (height && egl->procs.eglQueryWaylandBufferWL(egl->display,
			buffer, EGL_HEIGHT, height) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "Failed to get resource height");
		return false;
	}
	if (inverted_y && egl->procs.eglQueryWaylandBufferWL(egl->display,
			buffer, EGL_WAYLAND_Y_INVERTED_WL,
			inverted_y) != EGL_TRUE) {
		wlr_log(WLR_ERROR, "Failed to get resource inverted_y");
		return false;
	}

	return width || height || inverted_y;
}
