#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdlib.h>

#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "backend/fbdev.h"
#include "util/signal.h"

static struct wlr_fbdev_output *fbdev_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_fbdev(wlr_output));
	return (struct wlr_fbdev_output *)wlr_output;
}

/* Set width, height to 0 to use default resolution */
static bool create_fbo(struct wlr_fbdev_output *output, uint32_t width, uint32_t height) {
	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}

	int fd = output->backend->fd;
	struct fb_var_screeninfo scr_var;
	struct fb_fix_screeninfo scr_fix;

	ioctl(fd, FBIOGET_VSCREENINFO, &scr_var);
	ioctl(fd, FBIOGET_FSCREENINFO, &scr_fix);

	// Change resolution
	if (width && height) {
		scr_var.xres = width;
		scr_var.yres = height;
		if (ioctl(fd, FBIOPUT_VSCREENINFO, &scr_var) != 0) {
			wlr_log(WLR_ERROR, "Failed to set resolution %" PRIu32
				"x%" PRIu32, width, height);
			return false;
		}
	}

	// Map the device into memory
	size_t fbmem_size;
	if (scr_var.yres_virtual) {
		fbmem_size = scr_var.yres_virtual * scr_fix.line_length;
	} else {
		fbmem_size = scr_var.yres * scr_fix.line_length;
	}
	output->fbmem = mmap(NULL, fbmem_size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (output->fbmem == MAP_FAILED) {
		wlr_log(WLR_ERROR, "mmap failed");
		return false;
	}

	output->fbmem_size = fbmem_size;
	output->scr_var = scr_var;
	output->scr_fix = scr_fix;

	// Initialize GL renderbuffer and GL framebuffer
	GLuint rbo;
	glGenRenderbuffers(1, &rbo);
	glBindRenderbuffer(GL_RENDERBUFFER, rbo);
	glRenderbufferStorage(GL_RENDERBUFFER, output->backend->internal_format,
		scr_var.xres, scr_var.yres);
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
		munmap(output->fbmem, fbmem_size);
		return false;
	}

	output->fbo = fbo;
	output->rbo = rbo;
	return true;
}

static void destroy_fbo(struct wlr_fbdev_output *output) {
	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return;
	}

	munmap(output->fbmem, output->fbmem_size);

	glDeleteFramebuffers(1, &output->fbo);
	glDeleteRenderbuffers(1, &output->rbo);

	wlr_egl_unset_current(output->backend->egl);

	output->fbo = 0;
	output->rbo = 0;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, uint32_t width,
		uint32_t height, int32_t refresh) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);

	if (refresh <= 0) {
		refresh = FBDEV_DEFAULT_REFRESH;
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
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);

	if (!wlr_egl_make_current(output->backend->egl, EGL_NO_SURFACE, NULL)) {
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, output->fbo);

	if (buffer_age != NULL) {
		// HACK: set to 1 instead of 0 to avoid CPU burn
		// https://github.com/swaywm/wlroots/pull/2410#discussion_r497615821
		*buffer_age = 1;
	}
	return true;
}

static bool output_test(struct wlr_output *wlr_output) {
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_ENABLED) {
		wlr_log(WLR_DEBUG, "Cannot disable a fbdev output");
		return false;
	}

	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(wlr_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
	}

	return true;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);

	if (!output_test(wlr_output)) {
		return false;
	}

	if (!output->backend->session->active) {
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
		pixman_box32_t box = wlr_output->pending.damage.extents;
		int32_t box_width = box.x2 - box.x1;

		if (box_width) {
			GLint format;
			GLint type = GL_UNSIGNED_BYTE;
			glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &format);
			struct fb_var_screeninfo scr_var = output->scr_var;
			struct fb_fix_screeninfo scr_fix = output->scr_fix;
			unsigned bytes_per_pixel = (scr_var.bits_per_pixel + 7) / 8;

			wlr_log(WLR_DEBUG, "Updating box at %" PRIu32 "x%" PRIu32
				" size %" PRIu32 "x%" PRIu32, box.x1, box.y1,
				box_width, box.y2 - box.y1);
			for (int y = box.y1; y < box.y2; y++) {
				glReadPixels(box.x1, scr_var.yres - y - 1,
					box_width, 1, format, type,
					output->fbmem
					+ (scr_var.yoffset + y) * scr_fix.line_length
					+ (scr_var.xoffset + box.x1) * bytes_per_pixel);
			}
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		wlr_egl_unset_current(output->backend->egl);
		wlr_output_send_present(wlr_output, NULL);
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	}

	return true;
}

static void output_rollback_render(struct wlr_output *wlr_output) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);
	assert(wlr_egl_is_current(output->backend->egl));
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	wlr_egl_unset_current(output->backend->egl);
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);
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

bool wlr_output_is_fbdev(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static int signal_frame(void *data) {
	struct wlr_fbdev_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	return 0;
}

struct wlr_output *wlr_fbdev_add_output(struct wlr_backend *wlr_backend,
		uint32_t width, uint32_t height) {
	struct wlr_fbdev_backend *fbdev = fbdev_backend_from_backend(wlr_backend);

	struct wlr_fbdev_output *output = calloc(1, sizeof(struct wlr_fbdev_output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_fbdev_output");
		return NULL;
	}
	output->backend = fbdev;
	wlr_output_init(&output->wlr_output, &fbdev->backend, &output_impl,
		fbdev->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	if (!create_fbo(output, width, height)) {
		goto error;
	}

	// Width/height may be 0, but scr_var has the real resolution
	output_set_custom_mode(wlr_output, output->scr_var.xres,
		output->scr_var.yres, 0);
	strncpy(wlr_output->make, "fbdev", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "fbdev", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "FBDEV-%zd",
		++fbdev->last_output_num);

	char description[128];
	snprintf(description, sizeof(description),
		"Framebuffer output %zd (%s)", fbdev->last_output_num,
		output->scr_fix.id);
	wlr_output_set_description(wlr_output, description);

	if (!output_attach_render(wlr_output, NULL)) {
		goto error;
	}

	wlr_renderer_begin(fbdev->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(fbdev->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(fbdev->renderer);

	struct wl_event_loop *ev = wl_display_get_event_loop(fbdev->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&fbdev->outputs, &output->link);

	if (fbdev->started) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(wlr_output, true);
		wlr_signal_emit_safe(&fbdev->backend.events.new_output, wlr_output);
	}

	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
