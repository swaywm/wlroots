#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/renderer.h>
#include <wlr/render/renderer/gles.h>
#include <wlr/render/renderer/interface.h>
#include <wlr/render/shm.h>
#include <wlr/util/log.h>

#include "render/renderer/gles.h"

static const struct wlr_renderer_impl_2 gles_render_impl;

static struct wlr_gles *wlr_gles(struct wlr_renderer_2 *base) {
	assert(base->impl == &gles_render_impl);
	return (struct wlr_gles *)base;
}

static struct wlr_gles *wlr_gles_in_context(struct wlr_renderer_2 *base) {
	struct wlr_gles *gles = wlr_gles(base);
	assert(wlr_egl_is_current_2(gles->egl));
	return gles;
}

static void gles_destroy(struct wlr_renderer_2 *base) {
	if (!base)
		return;

	struct wlr_gles *gles = wlr_gles(base);
	wlr_egl_destroy_2(gles->egl);
	wlr_gbm_allocator_destroy(gles->gbm);
	free(gles);
}

static struct wlr_allocator *gles_get_allocator(struct wlr_renderer_2 *base) {
	struct wlr_gles *gles = wlr_gles(base);
	return &gles->gbm->base;
}

static void gles_bind_image(struct wlr_renderer_2 *rend_base, struct wlr_image *img_base) {
	struct wlr_gles *gles = wlr_gles(rend_base);
	struct wlr_gbm_image *img = (struct wlr_gbm_image *)img_base;
	struct wlr_gles_image *priv = img->renderer_priv;

	wlr_egl_make_current_2(gles->egl);

	glBindFramebuffer(GL_FRAMEBUFFER, priv->framebuffer);
	glViewport(0, 0, gbm_bo_get_width(img->bo), gbm_bo_get_height(img->bo));
}

static void gles_flush(struct wlr_renderer_2 *base, int *fence_out) {
	if (fence_out) {
		//glFlush();
		glFinish();

		*fence_out = -1;
	} else {
		glFinish();
	}
}

static void gles_clear(struct wlr_renderer_2 *base, const float color[static 4]) {
	// Call this to get the assertion
	wlr_gles_in_context(base);

	glClearColor(color[0], color[1], color[2], color[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

struct wlr_texture_2 *gles_texture_from_buffer(struct wlr_renderer_2 *base,
		struct wl_resource *buffer) {
	struct wlr_gles *gles = wlr_gles(base);
	struct wlr_gles_texture *tex = gles_texture_create(gles);
	if (!tex) {
		return NULL;
	}

	if (!wlr_texture_apply_damage_2(&tex->base, buffer, NULL)) {
		wlr_texture_destroy_2(&tex->base);
		return NULL;
	}

	return &tex->base;
}

static const struct wlr_renderer_impl_2 gles_render_impl = {
	.destroy = gles_destroy,
	.get_allocator = gles_get_allocator,
	.bind_image = gles_bind_image,
	.flush = gles_flush,
	.clear = gles_clear,
	.texture_from_buffer = gles_texture_from_buffer,
};

static bool gles_gbm_create(void *data, struct wlr_gbm_image *img) {
	struct wlr_gles *gles = data;

	struct wlr_gles_image *priv = calloc(1, sizeof(*priv));
	if (!priv) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return false;
	}

	wlr_egl_make_current_2(gles->egl);

	priv->egl = wlr_egl_create_image_2(gles->egl, img->bo);
	if (!priv->egl) {
		free(priv);
		return false;
	}

	glGenFramebuffers(1, &priv->framebuffer);
	glGenRenderbuffers(1, &priv->renderbuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, priv->renderbuffer);
	gles->egl_image_target_renderbuffer(GL_RENDERBUFFER, priv->egl);

	glBindFramebuffer(GL_FRAMEBUFFER, priv->framebuffer);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, priv->renderbuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	img->renderer_priv = priv;
	return true;
}

static void gles_gbm_destroy(void *data, struct wlr_gbm_image *img) {
	struct wlr_gles *gles = data;
	struct wlr_gles_image *priv = img->renderer_priv;

	glDeleteRenderbuffers(1, &priv->renderbuffer);
	glDeleteFramebuffers(1, &priv->framebuffer);
	wlr_egl_destroy_image_2(gles->egl, priv->egl);
	free(priv);
}

static bool gles_check_ext(const char *str, const char *ext) {
	while (1) {
		size_t len = strcspn(str, " ");

		if (strncmp(str, ext, len) == 0) {
			return true;
		}

		if (!str[len]) {
			return false;
		}

		str += len;
	}
}

struct wlr_renderer_2 *wlr_gles_renderer_create(struct wl_display *display,
		struct wlr_backend *backend) {
	int fd = wlr_backend_get_render_fd(backend);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Backend does not support GLES");
		return NULL;
	}

	struct wlr_gles *gles = calloc(1, sizeof(*gles));
	if (!gles) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	gles->display = display;
	gles->backend = backend;
	gles->gbm = wlr_gbm_allocator_create(fd, gles,
		gles_gbm_create, gles_gbm_destroy);
	if (!gles->gbm) {
		goto error_gles;
	}

	gles->egl = wlr_egl_create_2(gles->gbm->gbm);
	if (!gles->egl) {
		goto error_gbm;
	}

	wlr_egl_make_current_2(gles->egl);

	const char *exts = (const char *)glGetString(GL_EXTENSIONS);

	wlr_log(WLR_INFO, "Using %s", glGetString(GL_VERSION));
	wlr_log(WLR_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
	wlr_log(WLR_INFO, "Supported GLES2 extensions: %s", exts);

	if (!gles_check_ext(exts, "OES_surfaceless_context")) {
		wlr_log(WLR_ERROR, "GLES does not support surfaceless contexts");
		goto error_egl;
	}

	if (!gles_check_ext(exts, "OES_EGL_image_external")) {
		wlr_log(WLR_ERROR, "GLES does not support external EGL images");
		goto error_egl;
	}

	if (!gles_check_ext(exts, "OES_EGL_image")) {
		wlr_log(WLR_ERROR, "GLES does not support local EGL images");
		goto error_egl;
	}

	gles->egl_image_target_texture_2d =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	gles->egl_image_target_renderbuffer =
		(void *)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");

	if (!gles_check_ext(exts, "GL_EXT_texture_format_BGRA8888")) {
		wlr_log(WLR_ERROR, "GLES does not support BGRA8888");
		goto error_egl;
	}

	gles->has_texture_type_2_10_10_10_rev = gles_check_ext(exts,
		"GL_EXT_texture_type_2_10_10_10_REV");
	gles->has_required_internalformat =
		gles_check_ext(exts, "GL_OES_required_internalformat");
	gles->has_unpack_subimage = gles_check_ext(exts, "GL_EXT_unpack_subimage");

	if (gles_populate_shm_formats(gles)) {
		wlr_shm_init(gles->display, &gles->shm_formats);
	}

	wlr_renderer_init_2(&gles->base, &gles_render_impl);

	return &gles->base;

error_egl:
	wlr_egl_destroy_2(gles->egl);
error_gbm:
	wlr_gbm_allocator_destroy(gles->gbm);
error_gles:
	free(gles);
	return NULL;
}
