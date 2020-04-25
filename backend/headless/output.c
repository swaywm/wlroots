#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdlib.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "backend/headless.h"
#include "util/signal.h"

static struct wlr_headless_output *headless_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_headless(wlr_output));
	return (struct wlr_headless_output *)wlr_output;
}

static void dmabuf_attr_from_gbm_bo(struct wlr_dmabuf_attributes *attr,
		struct gbm_bo *bo) {
	assert(gbm_bo_get_plane_count(bo) == 1);

	memset(attr, 0, sizeof(*attr));

	attr->width = gbm_bo_get_width(bo);
	attr->height = gbm_bo_get_height(bo);
	attr->format = gbm_bo_get_format(bo);
	attr->n_planes = 1;
	attr->flags |= WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT;

	attr->fd[0] = gbm_bo_get_fd(bo);
	attr->stride[0] = gbm_bo_get_stride(bo);
	attr->offset[0] = gbm_bo_get_offset(bo, 0);
	attr->modifier = gbm_bo_get_modifier(bo);
}

static bool create_bo(struct wlr_headless_output *output,
		struct wlr_headless_bo *bo,
		unsigned int width, unsigned int height) {
	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}

	bo->bo = gbm_bo_create(output->backend->gbm, width, height,
		DRM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
	if (!bo->bo) {
		return false;
	}

	struct wlr_dmabuf_attributes attr;
	dmabuf_attr_from_gbm_bo(&attr, bo->bo);

	bool external_only = false;
	bo->image = wlr_egl_create_image_from_dmabuf(output->backend->egl, &attr,
			&external_only);
	wlr_dmabuf_attributes_finish(&attr);
	if (!bo->image) {
		gbm_bo_destroy(bo->bo);
		return false;
	}

	GLuint rbo = wlr_renderer_renderbuffer_from_image(
		output->backend->renderer, bo->image);

	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, rbo);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	wlr_egl_unset_current(output->backend->egl);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		gbm_bo_destroy(bo->bo);
		wlr_log(WLR_ERROR, "Failed to create FBO");
		return false;
	}

	bo->fbo = fbo;
	bo->rbo = rbo;
	return true;
}

static void destroy_bo(struct wlr_headless_output *output, struct wlr_headless_bo *bo) {
	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return;
	}

	glDeleteFramebuffers(1, &bo->fbo);
	glDeleteRenderbuffers(1, &bo->rbo);
	eglDestroyImage(output->backend->egl, bo->image);
	gbm_bo_destroy(bo->bo);
	wlr_egl_unset_current(output->backend->egl);
	bo->fbo = 0;
	bo->rbo = 0;
	bo->image = 0;
	bo->bo = NULL;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, int32_t width,
		int32_t height, int32_t refresh) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (refresh <= 0) {
		refresh = HEADLESS_DEFAULT_REFRESH;
	}

	destroy_bo(output, &output->bo[1]);
	destroy_bo(output, &output->bo[0]);
	if (!create_bo(output, &output->bo[0], width, height)) {
		wlr_output_destroy(wlr_output);
		return false;
	}
	if (!create_bo(output, &output->bo[1], width, height)) {
		wlr_output_destroy(wlr_output);
		return false;
	}

	output->front = &output->bo[0];
	output->back = &output->bo[1];

	output->frame_delay = 1000000 / refresh;

	wlr_output_update_custom_mode(&output->wlr_output, width, height, refresh);
	return true;
}

static bool output_attach_render(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}

	struct wlr_headless_bo *tmp = output->front;
	output->front = output->back;
	output->back = tmp;

	glBindFramebuffer(GL_FRAMEBUFFER, output->back->fbo);

	if (buffer_age != NULL) {
		*buffer_age = 0; // We only have one buffer
	}
	return true;
}

static bool output_test(struct wlr_output *wlr_output) {
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		wlr_log(WLR_DEBUG, "Cannot disable a headless output");
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(wlr_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
	}

	return true;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (!output_test(wlr_output)) {
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		if (!output_set_custom_mode(wlr_output,
				wlr_output->pending.custom_mode.width,
				wlr_output->pending.custom_mode.height,
				wlr_output->pending.custom_mode.refresh)) {
			return false;
		}
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		wlr_egl_unset_current(output->backend->egl);

		// Nothing needs to be done for FBOs
		wlr_output_send_present(wlr_output, NULL);
	}

	return true;
}

static bool output_export_dmabuf(struct wlr_output *wlr_output,
		struct wlr_dmabuf_attributes *attributes) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	struct wlr_egl_context saved_context;
	wlr_egl_save_context(&saved_context);
	wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL);
	glFinish();
	wlr_egl_restore_context(&saved_context);

	// Note: drm exports the back-buffer, so let's just do the same here.
	dmabuf_attr_from_gbm_bo(attributes, output->back->bo);
	return true;
}

static void output_rollback_render(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);
	assert(wlr_egl_is_current(output->backend->egl));
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	wlr_egl_unset_current(output->backend->egl);
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);
	wl_list_remove(&output->link);
	wl_event_source_remove(output->frame_timer);
	destroy_bo(output, &output->bo[1]);
	destroy_bo(output, &output->bo[0]);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.commit = output_commit,
	.rollback_render = output_rollback_render,
	.export_dmabuf = output_export_dmabuf,
};

bool wlr_output_is_headless(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static int signal_frame(void *data) {
	struct wlr_headless_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	return 0;
}

struct wlr_output *wlr_headless_add_output(struct wlr_backend *wlr_backend,
		unsigned int width, unsigned int height) {
	struct wlr_headless_backend *backend =
		headless_backend_from_backend(wlr_backend);

	struct wlr_headless_output *output =
		calloc(1, sizeof(struct wlr_headless_output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_headless_output");
		return NULL;
	}
	output->backend = backend;
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	if (!create_bo(output, &output->bo[0], width, height)) {
		goto error;
	}
	if (!create_bo(output, &output->bo[1], width, height)) {
		goto error;
	}

	output->front = &output->bo[0];
	output->back = &output->bo[1];

	output_set_custom_mode(wlr_output, width, height, 0);
	strncpy(wlr_output->make, "headless", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "headless", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "HEADLESS-%zd",
		++backend->last_output_num);

	char description[128];
	snprintf(description, sizeof(description),
		"Headless output %zd", backend->last_output_num);
	wlr_output_set_description(wlr_output, description);

	if (!output_attach_render(wlr_output, NULL)) {
		goto error;
	}

	wlr_renderer_begin(backend->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&backend->outputs, &output->link);

	if (backend->started) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	}

	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
