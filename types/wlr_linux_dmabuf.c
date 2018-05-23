#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_linux_dmabuf.h>
#include <wlr/util/log.h>
#include "linux-dmabuf-unstable-v1-protocol.h"
#include "util/signal.h"

#define LINUX_DMABUF_VERSION 3

static void buffer_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_buffer_interface buffer_impl = {
	.destroy = buffer_handle_destroy,
};

bool wlr_dmabuf_resource_is_buffer(struct wl_resource *buffer_resource) {
	if (!wl_resource_instance_of(buffer_resource, &wl_buffer_interface,
			&buffer_impl)) {
		return false;
	}

	struct wlr_dmabuf_buffer *buffer =
		wl_resource_get_user_data(buffer_resource);
	if (buffer && buffer->buffer_resource && !buffer->params_resource &&
			buffer->buffer_resource == buffer_resource) {
		return true;
	}

	return false;
}

struct wlr_dmabuf_buffer *wlr_dmabuf_buffer_from_buffer_resource(
		struct wl_resource *buffer_resource) {
	assert(wl_resource_instance_of(buffer_resource, &wl_buffer_interface,
		&buffer_impl));

	struct wlr_dmabuf_buffer *buffer =
		wl_resource_get_user_data(buffer_resource);
	assert(buffer);
	assert(buffer->buffer_resource);
	assert(!buffer->params_resource);
	assert(buffer->buffer_resource == buffer_resource);

	return buffer;
}

void wlr_dmabuf_buffer_attribs_finish(
		struct wlr_dmabuf_buffer_attribs *attribs) {
	for (int i = 0; i < attribs->n_planes; ++i) {
		close(attribs->fd[i]);
		attribs->fd[i] = -1;
	}
	attribs->n_planes = 0;
}

static void linux_dmabuf_buffer_destroy(struct wlr_dmabuf_buffer *buffer) {
	wlr_dmabuf_buffer_attribs_finish(&buffer->attributes);
	free(buffer);
}

static void params_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void params_add(struct wl_client *client,
		struct wl_resource *params_resource, int32_t name_fd,
		uint32_t plane_idx, uint32_t offset, uint32_t stride,
		uint32_t modifier_hi, uint32_t modifier_lo) {
	struct wlr_dmabuf_buffer *buffer =
		wlr_dmabuf_buffer_from_params_resource(params_resource);

	if (!buffer) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
			"params was already used to create a wl_buffer");
		close(name_fd);
		return;
	}

	if (plane_idx >= WLR_LINUX_DMABUF_MAX_PLANES) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
			"plane index %u > %u", plane_idx, WLR_LINUX_DMABUF_MAX_PLANES);
		close(name_fd);
		return;
	}

	if (buffer->attributes.fd[plane_idx] != -1) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
			"a dmabuf with id %d has already been added for plane %u",
			buffer->attributes.fd[plane_idx],
			plane_idx);
		close(name_fd);
		return;
	}

	buffer->attributes.fd[plane_idx] = name_fd;
	buffer->attributes.offset[plane_idx] = offset;
	buffer->attributes.stride[plane_idx] = stride;
	buffer->attributes.modifier[plane_idx] =
		((uint64_t)modifier_hi << 32) | modifier_lo;
	buffer->attributes.n_planes++;
}

static void buffer_handle_resource_destroy(struct wl_resource *buffer_resource) {
	struct wlr_dmabuf_buffer *buffer =
		wlr_dmabuf_buffer_from_buffer_resource(buffer_resource);
	linux_dmabuf_buffer_destroy(buffer);
}

static void params_create_common(struct wl_client *client,
		struct wl_resource *params_resource, uint32_t buffer_id, int32_t width,
		int32_t height, uint32_t format, uint32_t flags) {
	if (!wl_resource_get_user_data(params_resource)) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
			"params was already used to create a wl_buffer");
		return;
	}
	struct wlr_dmabuf_buffer *buffer =
		wlr_dmabuf_buffer_from_params_resource(params_resource);

	/* Switch the linux_dmabuf_buffer object from params resource to
	 * eventually wl_buffer resource. */
	wl_resource_set_user_data(buffer->params_resource, NULL);
	buffer->params_resource = NULL;

	if (!buffer->attributes.n_planes) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
			"no dmabuf has been added to the params");
		goto err_out;
	}

	if (buffer->attributes.fd[0] == -1) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
			"no dmabuf has been added for plane 0");
		goto err_out;
	}

	if ((buffer->attributes.fd[3] >= 0 || buffer->attributes.fd[2] >= 0) &&
			(buffer->attributes.fd[2] == -1 || buffer->attributes.fd[1] == -1)) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
			"gap in dmabuf planes");
		goto err_out;
	}

	buffer->attributes.width = width;
	buffer->attributes.height = height;
	buffer->attributes.format = format;
	buffer->attributes.flags = flags;

	if (width < 1 || height < 1) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
			"invalid width %d or height %d", width, height);
		goto err_out;
	}

	for (int i = 0; i < buffer->attributes.n_planes; i++) {
		if ((uint64_t)buffer->attributes.offset[i]
				+ buffer->attributes.stride[i] > UINT32_MAX) {
			wl_resource_post_error(params_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"size overflow for plane %d", i);
			goto err_out;
		}

		if ((uint64_t)buffer->attributes.offset[i]
				+ (uint64_t)buffer->attributes.stride[i] * height > UINT32_MAX) {
			wl_resource_post_error(params_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"size overflow for plane %d", i);
			goto err_out;
		}

		off_t size = lseek(buffer->attributes.fd[i], 0, SEEK_END);
		if (size == -1) { /* Skip checks if kernel does no support seek on buffer */
			continue;
		}
		if (buffer->attributes.offset[i] >= size) {
			wl_resource_post_error(params_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid offset %i for plane %d",
				buffer->attributes.offset[i], i);
			goto err_out;
		}

		if (buffer->attributes.offset[i] + buffer->attributes.stride[i]	> size) {
			wl_resource_post_error(params_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid stride %i for plane %d",
				buffer->attributes.stride[i], i);
			goto err_out;
		}

		if (i == 0 && /* planes > 0 might be subsampled according to fourcc format */
			buffer->attributes.offset[i] + buffer->attributes.stride[i] * height > size) {
			wl_resource_post_error(params_resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid buffer stride or height for plane %d", i);
			goto err_out;
		}
	}

	/* reject unknown flags */
	if (buffer->attributes.flags & ~ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT) {
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
			"Unknown dmabuf flags %"PRIu32, buffer->attributes.flags);
		goto err_out;
	}

	/* Check if dmabuf is usable */
	if (!wlr_renderer_check_import_dmabuf(buffer->renderer, buffer)) {
		goto err_failed;
	}

	buffer->buffer_resource = wl_resource_create(client, &wl_buffer_interface,
		1, buffer_id);
	if (!buffer->buffer_resource) {
		wl_resource_post_no_memory(params_resource);
		goto err_failed;
	}

	wl_resource_set_implementation(buffer->buffer_resource,
		&buffer_impl, buffer, buffer_handle_resource_destroy);

	/* send 'created' event when the request is not for an immediate
	 * import, that is buffer_id is zero */
	if (buffer_id == 0) {
		zwp_linux_buffer_params_v1_send_created(params_resource,
			buffer->buffer_resource);
	}
	return;

err_failed:
	if (buffer_id == 0) {
		zwp_linux_buffer_params_v1_send_failed(params_resource);
	} else {
		/* since the behavior is left implementation defined by the
		 * protocol in case of create_immed failure due to an unknown cause,
		 * we choose to treat it as a fatal error and immediately kill the
		 * client instead of creating an invalid handle and waiting for it
		 * to be used.
		 */
		wl_resource_post_error(params_resource,
			ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
			"importing the supplied dmabufs failed");
	}
err_out:
	linux_dmabuf_buffer_destroy(buffer);
}

static void params_create(struct wl_client *client,
		struct wl_resource *params_resource,
		int32_t width, int32_t height, uint32_t format, uint32_t flags) {
	params_create_common(client, params_resource, 0, width, height, format,
		flags);
}

static void params_create_immed(struct wl_client *client,
		struct wl_resource *params_resource, uint32_t buffer_id,
		int32_t width, int32_t height, uint32_t format, uint32_t flags) {
	params_create_common(client, params_resource, buffer_id, width, height,
		format, flags);
}

static const struct zwp_linux_buffer_params_v1_interface
		linux_buffer_params_impl = {
	.destroy = params_destroy,
	.add = params_add,
	.create = params_create,
	.create_immed = params_create_immed,
};

struct wlr_dmabuf_buffer *wlr_dmabuf_buffer_from_params_resource(
		struct wl_resource *params_resource) {
	assert(wl_resource_instance_of(params_resource,
		&zwp_linux_buffer_params_v1_interface,
		&linux_buffer_params_impl));

	struct wlr_dmabuf_buffer *buffer =
		wl_resource_get_user_data(params_resource);
	assert(buffer);
	assert(buffer->params_resource);
	assert(!buffer->buffer_resource);
	assert(buffer->params_resource == params_resource);

	return buffer;
}

static void handle_params_destroy(struct wl_resource *params_resource) {
	/* Check for NULL since wlr_dmabuf_buffer_from_params_resource will choke */
	if (!wl_resource_get_user_data(params_resource)) {
		return;
	}

	struct wlr_dmabuf_buffer *buffer =
		wlr_dmabuf_buffer_from_params_resource(params_resource);
	linux_dmabuf_buffer_destroy(buffer);
}

static void linux_dmabuf_create_params(struct wl_client *client,
		struct wl_resource *linux_dmabuf_resource,
		uint32_t params_id) {
	struct wlr_linux_dmabuf *linux_dmabuf =
		wlr_linux_dmabuf_from_resource(linux_dmabuf_resource);

	uint32_t version = wl_resource_get_version(linux_dmabuf_resource);
	struct wlr_dmabuf_buffer *buffer = calloc(1, sizeof *buffer);
	if (!buffer) {
		goto err;
	}

	for (int i = 0; i < WLR_LINUX_DMABUF_MAX_PLANES; i++) {
		buffer->attributes.fd[i] = -1;
	}

	buffer->renderer = linux_dmabuf->renderer;
	buffer->params_resource = wl_resource_create(client,
		&zwp_linux_buffer_params_v1_interface, version, params_id);
	if (!buffer->params_resource) {
		goto err_free;
	}

	wl_resource_set_implementation(buffer->params_resource,
		&linux_buffer_params_impl, buffer, handle_params_destroy);
	return;

err_free:
	free(buffer);
err:
	wl_resource_post_no_memory(linux_dmabuf_resource);
}

static void linux_dmabuf_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_impl = {
	.destroy = linux_dmabuf_destroy,
	.create_params = linux_dmabuf_create_params,
};

struct wlr_linux_dmabuf *wlr_linux_dmabuf_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_linux_dmabuf_v1_interface,
			&linux_dmabuf_impl));

	struct wlr_linux_dmabuf *dmabuf = wl_resource_get_user_data(resource);
	assert(dmabuf);
	return dmabuf;
}

static void linux_dmabuf_send_modifiers(struct wlr_linux_dmabuf *linux_dmabuf,
		struct wl_resource *resource) {
	struct wlr_renderer *renderer = linux_dmabuf->renderer;
	/*
	 * Use EGL_EXT_image_dma_buf_import_modifiers to query and advertise
	 * format/modifier codes.
	 */
	uint64_t modifier_invalid = DRM_FORMAT_MOD_INVALID;
	int *formats = NULL;
	int num_formats = wlr_renderer_get_dmabuf_formats(renderer, &formats);
	if (num_formats < 0) {
		return;
	}

	for (int i = 0; i < num_formats; i++) {
		uint64_t *modifiers = NULL;
		int num_modifiers = wlr_renderer_get_dmabuf_modifiers(renderer,
			formats[i], &modifiers);
		if (num_modifiers < 0) {
			return;
		}
		/* send DRM_FORMAT_MOD_INVALID token when no modifiers are supported
		 * for this format */
		if (num_modifiers == 0) {
			num_modifiers = 1;
			modifiers = &modifier_invalid;
		}
		for (int j = 0; j < num_modifiers; j++) {
			uint32_t modifier_lo = modifiers[j] & 0xFFFFFFFF;
			uint32_t modifier_hi = modifiers[j] >> 32;
			zwp_linux_dmabuf_v1_send_modifier(resource, formats[i],
				modifier_hi, modifier_lo);
		}
		if (modifiers != &modifier_invalid) {
			free(modifiers);
		}
	}
	free(formats);
}

static void linux_dmabuf_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void linux_dmabuf_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_linux_dmabuf *linux_dmabuf = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwp_linux_dmabuf_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &linux_dmabuf_impl,
		linux_dmabuf, linux_dmabuf_resource_destroy);
	wl_list_insert(&linux_dmabuf->wl_resources, wl_resource_get_link(resource));

	if (version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
		linux_dmabuf_send_modifiers(linux_dmabuf, resource);
	}
}

void wlr_linux_dmabuf_destroy(struct wlr_linux_dmabuf *linux_dmabuf) {
	if (!linux_dmabuf) {
		return;
	}

	wlr_signal_emit_safe(&linux_dmabuf->events.destroy, linux_dmabuf);

	wl_list_remove(&linux_dmabuf->display_destroy.link);
	wl_list_remove(&linux_dmabuf->renderer_destroy.link);

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &linux_dmabuf->wl_resources) {
		wl_resource_destroy(resource);
	}

	wl_global_destroy(linux_dmabuf->wl_global);
	free(linux_dmabuf);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_linux_dmabuf *linux_dmabuf =
		wl_container_of(listener, linux_dmabuf, display_destroy);
	wlr_linux_dmabuf_destroy(linux_dmabuf);
}

static void handle_renderer_destroy(struct wl_listener *listener, void *data) {
	struct wlr_linux_dmabuf *linux_dmabuf =
		wl_container_of(listener, linux_dmabuf, renderer_destroy);
	wlr_linux_dmabuf_destroy(linux_dmabuf);
}

struct wlr_linux_dmabuf *wlr_linux_dmabuf_create(struct wl_display *display,
		struct wlr_renderer *renderer) {
	struct wlr_linux_dmabuf *linux_dmabuf =
		calloc(1, sizeof(struct wlr_linux_dmabuf));
	if (linux_dmabuf == NULL) {
		wlr_log(L_ERROR, "could not create simple dmabuf manager");
		return NULL;
	}
	linux_dmabuf->renderer = renderer;

	wl_list_init(&linux_dmabuf->wl_resources);
	wl_signal_init(&linux_dmabuf->events.destroy);

	linux_dmabuf->wl_global =
		wl_global_create(display, &zwp_linux_dmabuf_v1_interface,
			LINUX_DMABUF_VERSION, linux_dmabuf, linux_dmabuf_bind);
	if (!linux_dmabuf->wl_global) {
		wlr_log(L_ERROR, "could not create linux dmabuf v1 wl global");
		free(linux_dmabuf);
		return NULL;
	}

	linux_dmabuf->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &linux_dmabuf->display_destroy);

	linux_dmabuf->renderer_destroy.notify = handle_renderer_destroy;
	wl_signal_add(&renderer->events.destroy, &linux_dmabuf->renderer_destroy);

	return linux_dmabuf;
}
