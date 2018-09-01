#include <assert.h>
#include <stdlib.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include "render/gles2.h"
#include <wayland-egl.h>
#include <gbm.h>

static const struct wlr_render_surface_impl render_surface_impl;
static const struct wlr_render_surface_impl gbm_render_surface_impl;

// render_surface util
struct wlr_gles2_render_surface *gles2_get_render_surface(
		struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &render_surface_impl ||
		wlr_rs->impl == &gbm_render_surface_impl);
	return (struct wlr_gles2_render_surface *)wlr_rs;
}

static struct wlr_gles2_gbm_render_surface *gles2_get_render_surface_gbm(
		struct wlr_render_surface *wlr_rs) {
	assert(wlr_rs->impl == &gbm_render_surface_impl);
	return (struct wlr_gles2_gbm_render_surface *)wlr_rs;
}

static void gles2_render_surface_finish(struct wlr_gles2_render_surface *rs) {
	wlr_egl_destroy_surface(&rs->renderer->egl, rs->surface);
	rs->surface = NULL;
	if (rs->egl_window) {
		wl_egl_window_destroy(rs->egl_window);
		rs->egl_window = NULL;
	}
}

// gles2_render_surface
static bool gles2_swap_buffers(struct wlr_render_surface *wlr_rs,
		pixman_region32_t *damage) {
	struct wlr_gles2_render_surface *rs = gles2_get_render_surface(wlr_rs);
	if (rs->pbuffer) {
		return true;
	}

	return wlr_egl_swap_buffers(&rs->renderer->egl, rs->surface, damage);
}

static void gles2_render_surface_resize(struct wlr_render_surface *wlr_rs,
		uint32_t width, uint32_t height) {
	struct wlr_gles2_render_surface *rs = gles2_get_render_surface(wlr_rs);
	if (width == rs->rs.width && height == rs->rs.height) {
		return;
	}

	rs->rs.width = width;
	rs->rs.height = height;
	if (rs->egl_window) {
		wl_egl_window_resize(rs->egl_window, width, height, 0, 0);
	}
}

static void gles2_render_surface_destroy(struct wlr_render_surface *wlr_rs) {
	struct wlr_gles2_render_surface *rs = gles2_get_render_surface(wlr_rs);
	gles2_render_surface_finish(rs);
	free(wlr_rs);
}

static int gles2_render_surface_buffer_age(struct wlr_render_surface *wlr_rs) {
	struct wlr_gles2_render_surface *rs = gles2_get_render_surface(wlr_rs);
	int ret = -1;
	if (!wlr_egl_make_current(&rs->renderer->egl, rs->surface,
			&ret)) {
		wlr_log(WLR_ERROR, "Failed to make egl current");
		return -1;
	}
	return ret;
}

static bool gles2_read_pixels(struct wlr_render_surface *wlr_rs,
		enum wl_shm_format wl_fmt, uint32_t *flags, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	struct wlr_gles2_render_surface *rs = gles2_get_render_surface(wlr_rs);
	if (!wlr_egl_make_current(&rs->renderer->egl, rs->surface, NULL)) {
		wlr_log(WLR_ERROR, "Failed to make egl current");
		return false;
	}

	const struct wlr_gles2_pixel_format *fmt = get_gles2_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(WLR_ERROR, "Cannot read pixels: unsupported pixel format");
		return false;
	}

	PUSH_GLES2_DEBUG;

	// Make sure any pending drawing is finished before we try to read it
	glFinish();

	glGetError(); // Clear the error flag

	unsigned char *p = ((unsigned char *)data) + dst_y * stride;
	uint32_t pack_stride = width * fmt->bpp / 8;
	if (pack_stride == stride && dst_x == 0 && flags != NULL) {
		// Under these particular conditions, we can read the pixels with only
		// one glReadPixels call
		glReadPixels(src_x, rs->rs.height - height - src_y,
			width, height, fmt->gl_format, fmt->gl_type, p);
		*flags = WLR_RENDERER_READ_PIXELS_Y_INVERT;
	} else {
		// Unfortunately GLES2 doesn't support GL_PACK_*, so we have to read
		// the lines out row by row
		for (size_t i = src_y; i < src_y + height; ++i) {
			glReadPixels(src_x, src_y + height - i - 1, width, 1, fmt->gl_format,
				fmt->gl_type, p + i * stride + dst_x * fmt->bpp / 8);
		}
		if (flags != NULL) {
			*flags = 0;
		}
	}

	POP_GLES2_DEBUG;

	return glGetError() == GL_NO_ERROR;
}

static const struct wlr_render_surface_impl render_surface_impl = {
	.buffer_age = gles2_render_surface_buffer_age,
	.destroy = gles2_render_surface_destroy,
	.swap_buffers = gles2_swap_buffers,
	.resize = gles2_render_surface_resize,
	.read_pixels = gles2_read_pixels,
};


// gles2_gbm_render_surface
static bool gles2_gbm_render_surface_init(
		struct wlr_gles2_gbm_render_surface *rs) {
	rs->gbm_surface = gbm_surface_create(rs->gbm_dev, rs->gles2_rs.rs.width,
		rs->gles2_rs.rs.height, GBM_FORMAT_ARGB8888, rs->flags);
	if (!rs->gbm_surface) {
		wlr_log_errno(WLR_ERROR, "Failed to create GBM surface");
		return false;
	}

	rs->gles2_rs.surface = wlr_egl_create_surface(&rs->gles2_rs.renderer->egl,
		rs->gbm_surface);
	return true;
}

static void gles2_gbm_render_surface_finish(
		struct wlr_gles2_gbm_render_surface *rs) {
	if (rs->gbm_surface) {
		if (rs->old_front_bo) {
			gbm_surface_release_buffer(rs->gbm_surface, rs->front_bo);
		}
		if (rs->front_bo) {
			gbm_surface_release_buffer(rs->gbm_surface, rs->old_front_bo);
		}
		gbm_surface_destroy(rs->gbm_surface);
	}
	gles2_render_surface_finish(&rs->gles2_rs);
}

static void gles2_gbm_render_surface_destroy(
		struct wlr_render_surface *wlr_rs) {
	struct wlr_gles2_gbm_render_surface *rs =
		gles2_get_render_surface_gbm(wlr_rs);
	gles2_gbm_render_surface_finish(rs);
	free(rs);
}

static bool gles2_gbm_render_surface_swap_buffers(
		struct wlr_render_surface *wlr_rs, pixman_region32_t* damage) {
	struct wlr_gles2_gbm_render_surface *rs =
		gles2_get_render_surface_gbm(wlr_rs);
	assert(rs->gbm_surface);
	if (rs->old_front_bo) {
		gbm_surface_release_buffer(rs->gbm_surface, rs->old_front_bo);
	}

	struct wlr_egl *egl = &rs->gles2_rs.renderer->egl;
	bool r = wlr_egl_swap_buffers(egl, rs->gles2_rs.surface, damage);
	rs->old_front_bo = rs->front_bo;
	rs->front_bo = gbm_surface_lock_front_buffer(rs->gbm_surface);
	return r;
}

static void gles2_gbm_render_surface_resize(
		struct wlr_render_surface *wlr_rs, uint32_t width, uint32_t height) {
	struct wlr_gles2_gbm_render_surface *rs =
		gles2_get_render_surface_gbm(wlr_rs);
	gles2_gbm_render_surface_finish(rs);
	rs->gles2_rs.rs.width = width;
	rs->gles2_rs.rs.height = height;
	gles2_gbm_render_surface_init(rs);
}

static struct gbm_bo *gles2_gbm_render_surface_get_bo(
		struct wlr_render_surface *wlr_rs) {
	struct wlr_gles2_gbm_render_surface *rs =
		gles2_get_render_surface_gbm(wlr_rs);
	return rs->front_bo;
}

static const struct wlr_render_surface_impl gbm_render_surface_impl = {
	.buffer_age = gles2_render_surface_buffer_age,
	.destroy = gles2_gbm_render_surface_destroy,
	.swap_buffers = gles2_gbm_render_surface_swap_buffers,
	.resize = gles2_gbm_render_surface_resize,
	.get_bo = gles2_gbm_render_surface_get_bo,
	.read_pixels = gles2_read_pixels,
};

// initialization
static struct wlr_gles2_render_surface *gles2_render_surface_create(
		struct wlr_renderer *wlr_renderer, uint32_t width, uint32_t height,
		void *egl_native_win) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);
	struct wlr_gles2_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->renderer = renderer;
	rs->rs.width = width;
	rs->rs.height = height;
	wlr_render_surface_init(&rs->rs, &render_surface_impl);

	if (egl_native_win) {
		rs->surface = wlr_egl_create_surface(&renderer->egl, egl_native_win);
		if (rs->surface == EGL_NO_SURFACE) {
			wlr_log(WLR_ERROR, "Failed to create EGL surface");
			free(rs);
			return NULL;
		}
	}

	return rs;
}

struct wlr_render_surface *gles2_render_surface_create_headless(
		struct wlr_renderer *renderer, uint32_t width, uint32_t height) {
	struct wlr_gles2_render_surface *rs = gles2_render_surface_create(renderer,
			width, height, NULL);
	if (!rs) {
		return NULL;
	}

	EGLint attribs[] = {
		EGL_WIDTH, rs->rs.width,
		EGL_HEIGHT, rs->rs.height, EGL_NONE};
	rs->surface = eglCreatePbufferSurface(rs->renderer->egl.display,
		rs->renderer->egl.config, attribs);
	rs->pbuffer = true;
	if (rs->surface == EGL_NO_SURFACE) {
		wlr_log(WLR_ERROR, "Failed to create EGL surface");
		free(rs);
		return NULL;
	}

	return &rs->rs;
}

struct wlr_render_surface *gles2_render_surface_create_xcb(
		struct wlr_renderer *renderer,
		uint32_t width, uint32_t height, void *xcb_connection,
		uint32_t xcb_window) {
	(void) xcb_connection;
	struct wlr_gles2_render_surface *rs =
		gles2_render_surface_create(renderer, width, height, &xcb_window);
	return rs ? &rs->rs : NULL;
}

struct wlr_render_surface *gles2_render_surface_create_wl(
		struct wlr_renderer *renderer,
		uint32_t width, uint32_t height,
		struct wl_display *disp, struct wl_surface *surf) {
	(void) disp;

	struct wl_egl_window *win = wl_egl_window_create(surf, width, height);
	if (!win) {
		wlr_log(WLR_ERROR, "wl_egl_window_create failed");
		return NULL;
	}

	struct wlr_gles2_render_surface *rs =
		gles2_render_surface_create(renderer, width, height, win);
	if (!rs) {
		wl_egl_window_destroy(win);
		return NULL;
	}

	rs->egl_window = win;
	return &rs->rs;
}

struct wlr_render_surface *gles2_render_surface_create_gbm(
		struct wlr_renderer *renderer, uint32_t width, uint32_t height,
		struct gbm_device *gbm_dev, uint32_t flags) {
	struct wlr_gles2_gbm_render_surface *rs = calloc(1, sizeof(*rs));
	if (!rs) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	rs->gles2_rs.renderer = gles2_get_renderer(renderer);
	rs->gles2_rs.rs.width = width;
	rs->gles2_rs.rs.height = height;
	wlr_render_surface_init(&rs->gles2_rs.rs, &gbm_render_surface_impl);

	rs->gbm_dev = gbm_dev;
	rs->flags = flags;

	if (!gles2_gbm_render_surface_init(rs)) {
		free(rs);
		return NULL;
	}

	return &rs->gles2_rs.rs;
}
