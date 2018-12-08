#ifndef RENDER_RENDERER_GLES_H
#define RENDER_RENDERER_GLES_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/render/allocator/gbm.h>
#include <wlr/render/egl.h>
#include <wlr/render/renderer.h>

struct wlr_gles {
	struct wlr_renderer_2 base;
	struct wlr_backend *backend;

	struct wlr_gbm_allocator *gbm;
	struct wlr_egl_2 *egl;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC egl_image_target_texture;
	PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC egl_image_target_renderbuffer;
};

struct wlr_gles_image {
	EGLImageKHR egl;
	GLuint framebuffer;
	GLuint renderbuffer;
};

#endif
