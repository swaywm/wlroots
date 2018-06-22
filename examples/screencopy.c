/*
 * Copyright © 2008 Kristian Høgsberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"

static struct wl_shm *shm = NULL;
static struct zwlr_screencopy_manager_v1 *screencopy_manager = NULL;
static struct wl_output *output = NULL;

static struct {
	struct wl_buffer *wl_buffer;
	void *data;
	int width, height, stride;
} buffer;
bool buffer_copy_done = false;

static int backingfile(off_t size) {
	char template[] = "/tmp/wlroots-shared-XXXXXX";
	int fd = mkstemp(template);
	if (fd < 0) {
		return -1;
	}

	int ret;
	while ((ret = ftruncate(fd, size)) == EINTR) {
		// No-op
	}
	if (ret < 0) {
		close(fd);
		return -1;
	}

	unlink(template);
	return fd;
}

static struct wl_buffer *create_shm_buffer(int width, int height,
		int *stride_out, void **data_out) {
	int stride = width * 4;
	int size = stride * height;

	int fd = backingfile(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
		return NULL;
	}

	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	close(fd);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);

	*data_out = data;
	*stride_out = stride;
	return buffer;
}

static void frame_handle_buffer(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t width, uint32_t height,
		uint32_t flags, uint32_t format, uint32_t stride) {
	buffer.width = width;
	buffer.height = height;
	buffer.wl_buffer =
		create_shm_buffer(width, height, &buffer.stride, &buffer.data);
	if (buffer.wl_buffer == NULL) {
		fprintf(stderr, "failed to create buffer\n");
		exit(EXIT_FAILURE);
	}

	zwlr_screencopy_frame_v1_copy(frame, buffer.wl_buffer);
}

static void frame_handle_ready(void *data,
		struct zwlr_screencopy_frame_v1 *frame, uint32_t tv_sec_hi,
		uint32_t tv_sec_lo, uint32_t tv_nsec) {
	buffer_copy_done = true;
}

static void frame_handle_failed(void *data,
		struct zwlr_screencopy_frame_v1 *frame) {
	fprintf(stderr, "failed to copy frame\n");
	exit(EXIT_FAILURE);
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
	.buffer = frame_handle_buffer,
	.ready = frame_handle_ready,
	.failed = frame_handle_failed,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_output_interface.name) == 0 && output == NULL) {
		output = wl_registry_bind(registry, name, &wl_output_interface, 1);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name)
			== 0) {
		screencopy_manager = wl_registry_bind(registry, name,
			&zwlr_screencopy_manager_v1_interface, 1);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// Who cares?
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void write_image(const char *filename, int width, int height, int stride,
		void *data) {
	char size[10 + 1 + 10 + 2 + 1]; // int32_t are max 10 digits
	sprintf(size, "%dx%d+0", width, height);

	int fd[2];
	if (pipe(fd) != 0) {
		fprintf(stderr, "cannot create pipe: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	pid_t child = fork();
	if (child < 0) {
		fprintf(stderr, "fork() failed\n");
		exit(EXIT_FAILURE);
	} else if (child != 0) {
		close(fd[0]);
		if (write(fd[1], data, stride * height) < 0) {
			fprintf(stderr, "write() failed: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		close(fd[1]);
		waitpid(child, NULL, 0);
	} else {
		close(fd[1]);
		if (dup2(fd[0], 0) != 0) {
			fprintf(stderr, "cannot dup the pipe\n");
			exit(EXIT_FAILURE);
		}
		close(fd[0]);
		// We requested WL_SHM_FORMAT_XRGB8888 in little endian, so that's BGRA
		// in big endian.
		execlp("convert", "convert", "-depth", "8", "-size", size, "bgra:-",
			"-alpha", "opaque", filename, NULL);
		fprintf(stderr, "cannot execute convert\n");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[]) {
	struct wl_display * display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (shm == NULL) {
		fprintf(stderr, "compositor is missing wl_shm\n");
		return EXIT_FAILURE;
	}
	if (screencopy_manager == NULL) {
		fprintf(stderr, "compositor doesn't support wlr-screencopy-unstable-v1\n");
		return EXIT_FAILURE;
	}
	if (output == NULL) {
		fprintf(stderr, "no output available\n");
		return EXIT_FAILURE;
	}

	struct zwlr_screencopy_frame_v1 *frame =
		zwlr_screencopy_manager_v1_capture_output(screencopy_manager, 0, output);
	zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, NULL);

	while (!buffer_copy_done) {
		wl_display_roundtrip(display);
	}

	write_image("wayland-screenshot.png", buffer.width, buffer.height,
		buffer.stride, buffer.data);
	wl_buffer_destroy(buffer.wl_buffer);

	return EXIT_SUCCESS;
}
