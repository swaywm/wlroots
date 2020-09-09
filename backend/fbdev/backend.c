#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/util/log.h>
#include "backend/fbdev.h"
#include "util/signal.h"

struct wlr_fbdev_backend *fbdev_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_fbdev(wlr_backend));
	return (struct wlr_fbdev_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	wlr_log(WLR_INFO, "Starting fbdev backend");

	struct wlr_fbdev_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(&output->wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output,
			&output->wlr_output);
	}

	struct wlr_fbdev_input_device *input_device;
	wl_list_for_each(input_device, &backend->input_devices,
			wlr_input_device.link) {
		wlr_signal_emit_safe(&backend->backend.events.new_input,
			&input_device->wlr_input_device);
	}

	backend->started = true;
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	if (!wlr_backend) {
		return;
	}

	wl_list_remove(&backend->display_destroy.link);
	wl_list_remove(&backend->renderer_destroy.link);

	struct wlr_fbdev_output *output, *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	struct wlr_fbdev_input_device *input_device, *input_device_tmp;
	wl_list_for_each_safe(input_device, input_device_tmp,
			&backend->input_devices, wlr_input_device.link) {
		wlr_input_device_destroy(&input_device->wlr_input_device);
	}

	wlr_signal_emit_safe(&wlr_backend->events.destroy, backend);

	if (backend->egl == &backend->priv_egl) {
		wlr_renderer_destroy(backend->renderer);
		wlr_egl_finish(&backend->priv_egl);
	}
	wlr_session_close_file(backend->session, backend->fd);
	free(backend);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	return backend->renderer;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_fbdev_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	backend_destroy(&backend->backend);
}

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct wlr_fbdev_backend *backend =
		wl_container_of(listener, backend, renderer_destroy);
	backend_destroy(&backend->backend);
}

static bool backend_init(struct wlr_fbdev_backend *fbdev,
		struct wl_display *display, struct wlr_session *session,
		int fbdev_fd, struct wlr_backend *parent,
		struct wlr_renderer *renderer) {
	wlr_backend_init(&fbdev->backend, &backend_impl);

	fbdev->display = display;
	fbdev->session = session;
	fbdev->fd = fbdev_fd;

	if (parent != NULL) {
		fbdev->parent = fbdev_backend_from_backend(parent);
	}

	wl_list_init(&fbdev->outputs);
	wl_list_init(&fbdev->input_devices);

	fbdev->renderer = renderer;
	fbdev->egl = wlr_gles2_renderer_get_egl(renderer);

	/* FIXME: hardcoded for now, there is no GL_ARGB4 etc, and translating
	 * would cost much CPU performance. Devices need to set this in kconfig
	 * for now. When wlroots has its own swapchain and software rendering,
	 * we can replace this with the appropriate pixman format like it's
	 * done in weston. */
	fbdev->internal_format = GL_RGBA4;

	fbdev->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &fbdev->display_destroy);

	wl_list_init(&fbdev->renderer_destroy.link);

	return true;
}

struct wlr_backend *wlr_fbdev_backend_create(struct wl_display *display,
		struct wlr_session *session, int fbdev_fd, struct wlr_backend *parent,
		wlr_renderer_create_func_t create_renderer_func) {
	assert(display && session && fbdev_fd >= 0);
	assert(!parent || wlr_backend_is_fbdev(parent));

	struct fb_fix_screeninfo scr_fix;
	ioctl(fbdev_fd, FBIOGET_FSCREENINFO, &scr_fix);
	wlr_log(WLR_INFO, "Initializing fbdev backend for %s", scr_fix.id);

	struct wlr_fbdev_backend *fbdev = calloc(1, sizeof(struct wlr_fbdev_backend));
	if (!fbdev) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_fbdev_backend");
		return NULL;
	}

	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_BLUE_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_RED_SIZE, 1,
		EGL_NONE,
	};

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	struct wlr_renderer *renderer = create_renderer_func(&fbdev->priv_egl,
		EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
		(EGLint*)config_attribs, 0);
	if (!renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		free(fbdev);
		return NULL;
	}

	if (!backend_init(fbdev, display, session, fbdev_fd, parent, renderer)) {
		wlr_renderer_destroy(fbdev->renderer);
		free(fbdev);
		return NULL;
	}

	fbdev->renderer_destroy.notify = handle_renderer_destroy;
	wl_signal_add(&renderer->events.destroy, &fbdev->renderer_destroy);

	return &fbdev->backend;
}

bool wlr_backend_is_fbdev(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
