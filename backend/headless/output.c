#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdlib.h>
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

static bool create_fbo(struct wlr_headless_output *output,
		unsigned int width, unsigned int height) {
	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}

	GLuint rbo;
	glGenRenderbuffers(1, &rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, output->backend->internal_format,
		width, height);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);

	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_RENDERBUFFER, rbo);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	wlr_egl_unset_current(output->backend->egl);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		wlr_log(WLR_ERROR, "Failed to create FBO");
		return false;
	}

	output->fbo = fbo;
	output->rbo = rbo;
	return true;
}

static void destroy_fbo(struct wlr_headless_output *output) {
	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return;
	}

	glDeleteFramebuffers(1, &output->fbo);
	glDeleteRenderbuffers(1, &output->rbo);

	wlr_egl_unset_current(output->backend->egl);

	output->fbo = 0;
	output->rbo = 0;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, int32_t width,
		int32_t height, int32_t refresh) {
	struct wlr_headless_output *output =
		headless_output_from_output(wlr_output);

	if (refresh <= 0) {
		refresh = HEADLESS_DEFAULT_REFRESH;
	}

	destroy_fbo(output);
	if (!create_fbo(output, width, height)) {
		wlr_output_destroy(wlr_output);
		return false;
	}

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

	glBindFramebuffer(GL_FRAMEBUFFER, output->fbo);

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
	destroy_fbo(output);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.commit = output_commit,
	.rollback_render = output_rollback_render,
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

	if (!create_fbo(output, width, height)) {
		goto error;
	}

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
