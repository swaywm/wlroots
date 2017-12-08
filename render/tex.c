#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>

#include <wlr/util/log.h>
#include "render/render.h"
#include "render/glapi.h"

bool wlr_tex_write_pixels(struct wlr_render *rend, struct wlr_tex *tex,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		const void *data) {
	assert(eglGetCurrentContext() == rend->egl->context);

	if (tex->type != WLR_TEX_GLTEX) {
		return false;
	}

	const struct format *fmt = wl_to_gl(wl_fmt);
	if (!fmt) {
		wlr_log(L_ERROR, "Unsupported pixel format");
		return false;
	}

	DEBUG_PUSH;

	glBindTexture(GL_TEXTURE_2D, tex->gl_tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (fmt->bpp / 8));
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, src_x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, src_y);

	glTexSubImage2D(GL_TEXTURE_2D, 0, dst_x, dst_y, width, height,
		fmt->gl_fmt, fmt->gl_type, data);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);

	DEBUG_POP;
	return true;
}

struct wlr_tex *wlr_tex_from_pixels(struct wlr_render *rend, enum wl_shm_format wl_fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data) {
	assert(eglGetCurrentContext() == rend->egl->context);

	const struct format *fmt = wl_to_gl(wl_fmt);
	if (!fmt) {
		wlr_log(L_ERROR, "Unsupported pixel format");
		return NULL;
	}

	struct wlr_tex *tex = calloc(1, sizeof(*tex));
	if (!tex) {
		wlr_log(L_ERROR, "Allocation failed");
		return NULL;
	}

	DEBUG_PUSH;

	tex->rend = rend;
	tex->width = width;
	tex->height = height;
	tex->type = WLR_TEX_GLTEX;

	glGenTextures(1, &tex->gl_tex);
	glBindTexture(GL_TEXTURE_2D, tex->gl_tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (fmt->bpp / 8));
	glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_fmt, width, height, 0,
		fmt->gl_fmt, fmt->gl_type, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	tex->image = eglCreateImageKHR(rend->egl->display, rend->egl->context,
		EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(uintptr_t)tex->gl_tex, NULL);
	if (tex->image == EGL_NO_IMAGE_KHR) {
		wlr_log(L_ERROR, "Failed to create EGL image: %s", egl_error());
		glDeleteTextures(1, &tex->gl_tex);
		free(tex);
		tex = NULL;
	} else {
		glGenTextures(1, &tex->image_tex);
		glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->image_tex);
		glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, tex->image);
	}

	DEBUG_POP;
	return tex;
}

struct wlr_tex *wlr_tex_from_wl_drm(struct wlr_render *rend, struct wl_resource *data) {
	EGLint fmt;
	if (!eglQueryWaylandBufferWL(rend->egl->display, data, EGL_TEXTURE_FORMAT, &fmt)) {
		wlr_log(L_ERROR, "Failed to get wayland DRM format: %s", egl_error());
		return NULL;
	}

	EGLint width;
	EGLint height;
	eglQueryWaylandBufferWL(rend->egl->display, data, EGL_WIDTH, &width);
	eglQueryWaylandBufferWL(rend->egl->display, data, EGL_HEIGHT, &height);

	struct wlr_tex *tex = calloc(1, sizeof(*tex));
	if (!tex) {
		wlr_log(L_ERROR, "Allocation failed");
		return NULL;
	}

	tex->rend = rend;
	tex->width = width;
	tex->height = height;
	tex->type = WLR_TEX_WLDRM;
	tex->wl_drm = data;

	EGLint attribs[] = {
		EGL_WAYLAND_PLANE_WL, 0,
		EGL_NONE,
	};

	tex->image = eglCreateImageKHR(rend->egl->display, rend->egl->context,
		EGL_WAYLAND_BUFFER_WL, data, attribs);
	if (!tex->image) {
		wlr_log(L_ERROR, "Failed to create EGL image: %s", egl_error());
		free(tex);
		return NULL;
	}

	DEBUG_PUSH;

	glGenTextures(1, &tex->image_tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->image_tex);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, tex->image);

	DEBUG_POP;
	return tex;
}

// TODO: Modify to allow multi-planar formats
struct wlr_tex *wlr_tex_from_dmabuf(struct wlr_render *rend, uint32_t fourcc_fmt,
		uint32_t width, uint32_t height, int fd0, uint32_t offset0, uint32_t stride0) {
	struct wlr_tex *tex = calloc(1, sizeof(*tex));
	if (!tex) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return false;
	}

	tex->rend = rend;
	tex->width = width;
	tex->height = height;
	tex->type = WLR_TEX_DMABUF;
	tex->dmabuf = fd0;

	EGLint attribs[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_LINUX_DRM_FOURCC_EXT, fourcc_fmt,
		EGL_DMA_BUF_PLANE0_FD_EXT, fd0,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, stride0,
		EGL_IMAGE_PRESERVED_KHR, EGL_FALSE,
		EGL_NONE,
	};

	tex->image = eglCreateImageKHR(rend->egl->display, EGL_NO_CONTEXT,
		EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (!tex->image) {
		wlr_log(L_ERROR, "Failed to create EGL image: %s", egl_error());
		free(tex);
		return NULL;
	}

	DEBUG_PUSH;

	glGenTextures(1, &tex->image_tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->image_tex);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, tex->image);

	DEBUG_POP;
	return tex;
}


void wlr_tex_destroy(struct wlr_tex *tex) {
	if (!tex) {
		return;
	}

	struct wlr_render *rend = tex->rend;

	eglMakeCurrent(rend->egl->display, EGL_NO_SURFACE,
		EGL_NO_SURFACE, rend->egl->context);

	DEBUG_PUSH;

	glDeleteTextures(1, &tex->image_tex);
	eglDestroyImageKHR(rend->egl->display, tex->image);

	switch (tex->type) {
	case WLR_TEX_GLTEX:
		glDeleteTextures(1, &tex->gl_tex);
		break;
	case WLR_TEX_DMABUF:
		close(tex->dmabuf);
		break;
	default:
		break;
	}

	free(tex);

	DEBUG_POP;
}
