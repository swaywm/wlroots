#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"
#include "util/signal.h"

static struct gles2_pixel_format external_pixel_format = {
	.wl_format = 0,
	.depth = 0,
	.bpp = 0,
	.gl_format = 0,
	.gl_type = 0,
};

static void gles2_texture_ensure(struct wlr_gles2_texture *texture,
		GLenum target) {
	if (texture->tex_id) {
		return;
	}
	texture->target = target;
	glGenTextures(1, &texture->tex_id);
	glBindTexture(target, texture->tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static const struct wlr_texture_impl texture_impl;

struct wlr_gles2_texture *gles2_get_texture(struct wlr_texture *wlr_texture) {
	assert(wlr_texture->impl == &texture_impl);
	return (struct wlr_gles2_texture *)wlr_texture;
}

static bool gles2_texture_upload_pixels(struct wlr_texture *wlr_texture,
		enum wl_shm_format format, int stride, int width, int height,
		const unsigned char *pixels) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	const struct gles2_pixel_format *fmt = gles2_format_from_wl(format);
	if (!fmt || !fmt->gl_format) {
		wlr_log(L_ERROR, "No supported pixel format for this texture");
		return false;
	}
	texture->wlr_texture.width = width;
	texture->wlr_texture.height = height;
	texture->wlr_texture.format = format;
	texture->pixel_format = fmt;

	GLES2_DEBUG_PUSH;
	gles2_texture_ensure(texture, GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture->tex_id);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride);
	glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
		fmt->gl_format, fmt->gl_type, pixels);
	GLES2_DEBUG_POP;

	texture->wlr_texture.valid = true;
	return true;
}

static bool gles2_texture_update_pixels(struct wlr_texture *wlr_texture,
		enum wl_shm_format format, int stride, int x, int y,
		int width, int height, const unsigned char *pixels) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	// TODO: Test if the unpack subimage extension is supported and adjust the
	// upload strategy if not
	if (!texture->wlr_texture.valid
			|| texture->wlr_texture.format != format
		/*	|| unpack not supported */) {
		return gles2_texture_upload_pixels(&texture->wlr_texture, format,
			stride, width, height, pixels);
	}
	const struct gles2_pixel_format *fmt = texture->pixel_format;
	GLES2_DEBUG_PUSH;
	glBindTexture(GL_TEXTURE_2D, texture->tex_id);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, y);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, fmt->gl_format,
		fmt->gl_type, pixels);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
	GLES2_DEBUG_POP;
	return true;
}

static bool gles2_texture_upload_shm(struct wlr_texture *wlr_texture,
		uint32_t format, struct wl_shm_buffer *buffer) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	const struct gles2_pixel_format *fmt = gles2_format_from_wl(format);
	if (!fmt || !fmt->gl_format) {
		wlr_log(L_ERROR, "Unsupported pixel format %"PRIu32" for this texture",
				format);
		return false;
	}
	wl_shm_buffer_begin_access(buffer);
	uint8_t *pixels = wl_shm_buffer_get_data(buffer);
	int width = wl_shm_buffer_get_width(buffer);
	int height = wl_shm_buffer_get_height(buffer);
	int pitch = wl_shm_buffer_get_stride(buffer) / (fmt->bpp / 8);
	texture->wlr_texture.width = width;
	texture->wlr_texture.height = height;
	texture->wlr_texture.format = format;
	texture->pixel_format = fmt;

	GLES2_DEBUG_PUSH;
	gles2_texture_ensure(texture, GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture->tex_id);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
		fmt->gl_format, fmt->gl_type, pixels);
	GLES2_DEBUG_POP;

	texture->wlr_texture.valid = true;
	wl_shm_buffer_end_access(buffer);
	return true;
}

static bool gles2_texture_update_shm(struct wlr_texture *wlr_texture,
		uint32_t format, int x, int y, int width, int height,
		struct wl_shm_buffer *buffer) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	// TODO: Test if the unpack subimage extension is supported and adjust the
	// upload strategy if not
	assert(texture);
	if (!texture->wlr_texture.valid
			|| texture->wlr_texture.format != format
		/*	|| unpack not supported */) {
		return gles2_texture_upload_shm(&texture->wlr_texture, format, buffer);
	}
	const struct gles2_pixel_format *fmt = texture->pixel_format;
	wl_shm_buffer_begin_access(buffer);
	uint8_t *pixels = wl_shm_buffer_get_data(buffer);
	int pitch = wl_shm_buffer_get_stride(buffer) / (fmt->bpp / 8);

	GLES2_DEBUG_PUSH;
	glBindTexture(GL_TEXTURE_2D, texture->tex_id);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, pitch);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, y);
	glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height,
			fmt->gl_format, fmt->gl_type, pixels);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);
	GLES2_DEBUG_POP;

	wl_shm_buffer_end_access(buffer);

	return true;
}

static bool gles2_texture_upload_drm(struct wlr_texture *wlr_texture,
		struct wl_resource *buf) {
	struct wlr_gles2_texture *tex = gles2_get_texture(wlr_texture);
	if (!glEGLImageTargetTexture2DOES) {
		return false;
	}

	EGLint format;
	if (!wlr_egl_query_buffer(tex->egl, buf, EGL_TEXTURE_FORMAT, &format)) {
		wlr_log(L_INFO, "upload_drm called with no drm buffer");
		return false;
	}

	wlr_egl_query_buffer(tex->egl, buf, EGL_WIDTH,
		(EGLint*)&tex->wlr_texture.width);
	wlr_egl_query_buffer(tex->egl, buf, EGL_HEIGHT,
		(EGLint*)&tex->wlr_texture.height);

	EGLint inverted_y;
	if (wlr_egl_query_buffer(tex->egl, buf, EGL_WAYLAND_Y_INVERTED_WL,
			&inverted_y)) {
		tex->wlr_texture.inverted_y = !!inverted_y;
	}

	GLenum target;
	const struct gles2_pixel_format *pf;
	switch (format) {
	case EGL_TEXTURE_RGB:
	case EGL_TEXTURE_RGBA:
		target = GL_TEXTURE_2D;
		pf = gles2_format_from_wl(WL_SHM_FORMAT_ARGB8888);
		break;
	case EGL_TEXTURE_EXTERNAL_WL:
		target = GL_TEXTURE_EXTERNAL_OES;
		pf = &external_pixel_format;
		break;
	default:
		wlr_log(L_ERROR, "invalid/unsupported egl buffer format");
		return false;
	}

	GLES2_DEBUG_PUSH;
	gles2_texture_ensure(tex, target);
	glBindTexture(GL_TEXTURE_2D, tex->tex_id);
	GLES2_DEBUG_POP;

	EGLint attribs[] = { EGL_WAYLAND_PLANE_WL, 0, EGL_NONE };

	if (tex->image) {
		wlr_egl_destroy_image(tex->egl, tex->image);
	}

	tex->image = wlr_egl_create_image(tex->egl, EGL_WAYLAND_BUFFER_WL,
		(EGLClientBuffer*) buf, attribs);
	if (!tex->image) {
		wlr_log(L_ERROR, "failed to create EGL image");
 		return false;
	}

	GLES2_DEBUG_PUSH;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(target, tex->tex_id);
	glEGLImageTargetTexture2DOES(target, tex->image);
	GLES2_DEBUG_POP;
	tex->wlr_texture.valid = true;
	tex->pixel_format = pf;

	return true;
}

static bool gles2_texture_upload_eglimage(struct wlr_texture *wlr_texture,
		EGLImageKHR image, uint32_t width, uint32_t height) {
	struct wlr_gles2_texture *tex = gles2_get_texture(wlr_texture);

	tex->image = image;
	tex->pixel_format = &external_pixel_format;
	tex->wlr_texture.valid = true;
	tex->wlr_texture.width = width;
	tex->wlr_texture.height = height;

	GLES2_DEBUG_PUSH;
	gles2_texture_ensure(tex, GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex->tex_id);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, tex->image);
	GLES2_DEBUG_POP;

	return true;
}

static bool gles2_texture_upload_dmabuf(struct wlr_texture *wlr_texture,
		struct wl_resource *dmabuf_resource) {
	struct wlr_gles2_texture *tex = gles2_get_texture(wlr_texture);
	struct wlr_dmabuf_buffer *dmabuf =
		wlr_dmabuf_buffer_from_buffer_resource(dmabuf_resource);

	if (!tex->egl->egl_exts.dmabuf_import) {
		wlr_log(L_ERROR, "Want dmabuf but extension not present");
		return false;
	}

	tex->wlr_texture.width = dmabuf->attributes.width;
	tex->wlr_texture.height = dmabuf->attributes.height;

	if (tex->image) {
		wlr_egl_destroy_image(tex->egl, tex->image);
	}

	if (wlr_dmabuf_buffer_has_inverted_y(dmabuf)) {
		wlr_texture->inverted_y = true;
	}

	GLenum target = GL_TEXTURE_2D;
	const struct gles2_pixel_format *pf =
		gles2_format_from_wl(WL_SHM_FORMAT_ARGB8888);
	GLES2_DEBUG_PUSH;
	gles2_texture_ensure(tex, target);
	glBindTexture(target, tex->tex_id);
	tex->image = wlr_egl_create_image_from_dmabuf(tex->egl, &dmabuf->attributes);
	glActiveTexture(GL_TEXTURE0);
	glEGLImageTargetTexture2DOES(target, tex->image);
	GLES2_DEBUG_POP;
	tex->pixel_format = pf;
	tex->wlr_texture.valid = true;
	return true;
}

static bool gles2_texture_get_dmabuf_size(struct wlr_texture *texture, struct
		wl_resource *resource, int *width, int *height) {
	if (!wlr_dmabuf_resource_is_buffer(resource)) {
		return false;
	}

	struct wlr_dmabuf_buffer *dmabuf =
		wlr_dmabuf_buffer_from_buffer_resource(resource);
	*width = dmabuf->attributes.width;
	*height = dmabuf->attributes.height;
	return true;
}

static void gles2_texture_get_buffer_size(struct wlr_texture *texture,
		struct wl_resource *resource, int *width, int *height) {
	struct wl_shm_buffer *buffer = wl_shm_buffer_get(resource);
	if (!buffer) {
		struct wlr_gles2_texture *tex = (struct wlr_gles2_texture *)texture;
		if (!glEGLImageTargetTexture2DOES) {
			return;
		}
		if (!wlr_egl_query_buffer(tex->egl, resource, EGL_WIDTH,
				(EGLint*)width)) {
			if (!gles2_texture_get_dmabuf_size(texture, resource, width,
					height)) {
				wlr_log(L_ERROR, "could not get size of the buffer");
				return;
			}
		}
		wlr_egl_query_buffer(tex->egl, resource, EGL_HEIGHT, (EGLint*)height);

		return;
	}

	*width = wl_shm_buffer_get_width(buffer);
	*height = wl_shm_buffer_get_height(buffer);
}

static void gles2_texture_destroy(struct wlr_texture *wlr_texture) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	wlr_signal_emit_safe(&texture->wlr_texture.destroy_signal,
		&texture->wlr_texture);
	if (texture->tex_id) {
		GLES2_DEBUG_PUSH;
		glDeleteTextures(1, &texture->tex_id);
		GLES2_DEBUG_POP;
	}

	if (texture->image) {
		wlr_egl_destroy_image(texture->egl, texture->image);
	}

	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.upload_pixels = gles2_texture_upload_pixels,
	.update_pixels = gles2_texture_update_pixels,
	.upload_shm = gles2_texture_upload_shm,
	.update_shm = gles2_texture_update_shm,
	.upload_drm = gles2_texture_upload_drm,
	.upload_dmabuf = gles2_texture_upload_dmabuf,
	.upload_eglimage = gles2_texture_upload_eglimage,
	.get_buffer_size = gles2_texture_get_buffer_size,
	.destroy = gles2_texture_destroy,
};

struct wlr_texture *gles2_texture_create(struct wlr_egl *egl) {
	struct wlr_gles2_texture *texture;
	if (!(texture = calloc(1, sizeof(struct wlr_gles2_texture)))) {
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->egl = egl;
	return &texture->wlr_texture;
}
