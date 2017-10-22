#include <assert.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>

#include <wlr/util/log.h>
#include "render/render.h"
#include "render/glapi.h"

struct wlr_tex *wlr_tex_from_pixels(struct wlr_render *rend, enum wl_shm_format fmt,
		uint32_t stride, uint32_t width, uint32_t height, const void *data) {
	assert(eglGetCurrentContext() == rend->egl->context);

	GLuint gl_format;
	GLuint gl_type;

	switch (fmt) {
	case WL_SHM_FORMAT_ARGB8888:
		gl_format = GL_BGRA_EXT;
		gl_type = GL_UNSIGNED_BYTE;
		break;
	case WL_SHM_FORMAT_XRGB8888:
		gl_format = GL_BGRA_EXT;
		gl_type = GL_UNSIGNED_BYTE;
		break;
	case WL_SHM_FORMAT_ABGR8888:
		gl_format = GL_RGBA;
		gl_type = GL_UNSIGNED_BYTE;
		break;
	case WL_SHM_FORMAT_XBGR8888:
		gl_format = GL_RGBA;
		gl_type = GL_UNSIGNED_BYTE;
		break;
	default:
		wlr_log(L_ERROR, "Unsupported pixel format");
		return NULL;
	};

	struct wlr_tex *tex = calloc(1, sizeof(*tex));
	if (!tex) {
		wlr_log(L_ERROR, "Allocation failed");
		return NULL;
	}

	DEBUG_PUSH;

	tex->rend = rend;
	tex->fmt = fmt;
	tex->width = width;
	tex->height = height;
	tex->type = WLR_TEX_GLTEX;

	glGenTextures(1, &tex->gl_tex);
	glBindTexture(GL_TEXTURE_2D, tex->gl_tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride);
	glTexImage2D(GL_TEXTURE_2D, 0, gl_format, width, height, 0,
		gl_format, gl_type, data);
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
