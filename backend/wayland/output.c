#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <GLES3/gl3.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"

int os_create_anonymous_file(off_t size);

static struct wl_callback_listener frame_listener;

static void surface_frame_callback(void *data, struct wl_callback *cb, uint32_t time) {
	struct wlr_output *wlr_output = data;
	assert(wlr_output);
	wl_signal_emit(&wlr_output->events.frame, wlr_output);
	wl_callback_destroy(cb);
}

static struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

static void wlr_wl_output_make_current(struct wlr_output *_output) {
	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	if (!eglMakeCurrent(output->backend->egl.display,
		output->egl_surface, output->egl_surface,
		output->backend->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
	}
}

static void wlr_wl_output_swap_buffers(struct wlr_output *_output) {
	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);
	if (!eglSwapBuffers(output->backend->egl.display, output->egl_surface)) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
	}
}

static void wlr_wl_output_transform(struct wlr_output *_output,
		enum wl_output_transform transform) {
	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	output->wlr_output.transform = transform;
}

static bool wlr_wl_output_set_cursor(struct wlr_output *_output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {

	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	struct wlr_wl_backend *backend = output->backend;
	stride *= 4; // stride is given in pixels, we need it in bytes

	if (!backend->shm || !backend->pointer) {
		wlr_log(L_INFO, "cannot set cursor, no shm or pointer");
		return false;
	}

	if (!output->cursor_surface) {
		output->cursor_surface = wl_compositor_create_surface(output->backend->compositor);
	}

	uint32_t size = stride * height;
	if (output->cursor_buf_size != size) {
		if (output->cursor_buffer) {
			wl_buffer_destroy(output->cursor_buffer);
		}

		if (size > output->cursor_buf_size) {
			if (output->cursor_pool) {
				wl_shm_pool_destroy(output->cursor_pool);
				output->cursor_pool = NULL;
				munmap(output->cursor_data, output->cursor_buf_size);
			}
		}

		if (!output->cursor_pool) {
			int fd = os_create_anonymous_file(size);
			if (fd < 0) {
				wlr_log_errno(L_INFO, "creating anonymous file for cursor buffer failed");
				return false;
			}

			output->cursor_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			if (output->cursor_data == MAP_FAILED) {
				close(fd);
				wlr_log_errno(L_INFO, "mmap failed");
				return false;
			}

			output->cursor_pool = wl_shm_create_pool(backend->shm, fd, size);
			close(fd);
		}

		output->cursor_buffer = wl_shm_pool_create_buffer(output->cursor_pool,
			0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
		output->cursor_buf_size = size;
	}

	memcpy(output->cursor_data, buf, size);
	wl_surface_attach(output->cursor_surface, output->cursor_buffer, 0, 0);
	wl_surface_damage(output->cursor_surface, 0, 0, width, height);
	wl_surface_commit(output->cursor_surface);

	wlr_wl_output_update_cursor(output, output->enter_serial);
	return true;
}

static void wlr_wl_output_destroy(struct wlr_output *_output) {
	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	wl_signal_emit(&output->backend->backend.events.output_remove, &output->wlr_output);

	if (output->cursor_buf_size != 0) {
		assert(output->cursor_data);
		assert(output->cursor_buffer);
		assert(output->cursor_pool);

		wl_buffer_destroy(output->cursor_buffer);
		munmap(output->cursor_data, output->cursor_buf_size);
		wl_shm_pool_destroy(output->cursor_pool);
	}

	if (output->cursor_surface) {
		wl_surface_destroy(output->cursor_surface);
	}

	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}
	eglDestroySurface(output->backend->egl.display, output->surface);
	wl_egl_window_destroy(output->egl_window);
	wl_shell_surface_destroy(output->shell_surface);
	wl_surface_destroy(output->surface);
	free(output);
}

void wlr_wl_output_update_cursor(struct wlr_wl_backend_output *output, uint32_t serial) {
	if (output->cursor_surface && output->backend->pointer && serial) {
		wl_pointer_set_cursor(output->backend->pointer, serial,
			output->cursor_surface, 0, 0);
	}
}

bool wlr_wl_output_move_cursor(struct wlr_output *_output, int x, int y) {
	// TODO: only return true if x == current x and y == current y
	return true;
}

static struct wlr_output_impl output_impl = {
	.transform = wlr_wl_output_transform,
	.destroy = wlr_wl_output_destroy,
	.make_current = wlr_wl_output_make_current,
	.swap_buffers = wlr_wl_output_swap_buffers,
	.set_cursor = wlr_wl_output_set_cursor,
	.move_cursor = wlr_wl_output_move_cursor
};

void handle_ping(void* data, struct wl_shell_surface* ssurface, uint32_t serial) {
	struct wlr_wl_backend_output *output = data;
	assert(output && output->shell_surface == ssurface);
	wl_shell_surface_pong(ssurface, serial);
}

void handle_configure(void *data, struct wl_shell_surface *wl_shell_surface,
		uint32_t edges, int32_t width, int32_t height){
	struct wlr_wl_backend_output *output = data;
	assert(output && output->shell_surface == wl_shell_surface);
	wl_egl_window_resize(output->egl_window, width, height, 0, 0);
	output->wlr_output.width = width;
	output->wlr_output.height = height;
	wlr_output_update_matrix(&output->wlr_output);
	wl_signal_emit(&output->wlr_output.events.resolution, output);
}

void handle_popup_done(void *data, struct wl_shell_surface *wl_shell_surface) {
	wlr_log(L_ERROR, "Unexpected wl_shell_surface.popup_done event");
}

static struct wl_shell_surface_listener shell_surface_listener = {
	.ping = handle_ping,
	.configure = handle_configure,
	.popup_done = handle_popup_done
};

struct wlr_output *wlr_wl_output_create(struct wlr_backend *_backend) {
	assert(wlr_backend_is_wl(_backend));
	struct wlr_wl_backend *backend = (struct wlr_wl_backend *)_backend;
	if (!backend->remote_display) {
		++backend->requested_outputs;
		return NULL;
	}

	struct wlr_wl_backend_output *output;
	if (!(output = calloc(sizeof(struct wlr_wl_backend_output), 1))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_backend_output");
		return NULL;
	}
	wlr_output_init(&output->wlr_output, &output_impl);
	struct wlr_output *wlr_output = &output->wlr_output;

	wlr_output->width = 640;
	wlr_output->height = 480;
	wlr_output->scale = 1;
	strncpy(wlr_output->make, "wayland", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "wayland", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "WL-%zd",
			backend->outputs->length + 1);
	wlr_output_update_matrix(wlr_output);

	output->backend = backend;

	// TODO: error handling
	output->surface = wl_compositor_create_surface(backend->compositor);
	output->shell_surface =
		wl_shell_get_shell_surface(backend->shell, output->surface);

	wl_shell_surface_set_class(output->shell_surface, "sway");
	wl_shell_surface_set_title(output->shell_surface, "sway-wl");
	wl_shell_surface_add_listener(output->shell_surface,
			&shell_surface_listener, output);
	wl_shell_surface_set_toplevel(output->shell_surface);

	output->egl_window = wl_egl_window_create(output->surface,
			wlr_output->width, wlr_output->height);
	output->egl_surface = wlr_egl_create_surface(&backend->egl, output->egl_window);

	// start rendering loop per callbacks by rendering first frame
	if (!eglMakeCurrent(output->backend->egl.display,
		output->egl_surface, output->egl_surface,
		output->backend->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
		return false;
	}

	glViewport(0, 0, wlr_output->width, wlr_output->height);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	if (!eglSwapBuffers(output->backend->egl.display, output->egl_surface)) {
		wlr_log(L_ERROR, "eglSwapBuffers failed: %s", egl_error());
		return false;
	}

	wlr_output_create_global(wlr_output, backend->local_display);
	list_add(backend->outputs, wlr_output);
	wl_signal_emit(&backend->backend.events.output_add, wlr_output);
	return wlr_output;
}
