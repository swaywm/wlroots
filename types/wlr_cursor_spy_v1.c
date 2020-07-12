#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_cursor_spy_v1.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>
#include "cursor-spy-unstable-v1-protocol.h"
#include "util/signal.h"

#define CURSOR_SPY_MANAGER_VERSION 1
#define PREFERRED_FORMAT WL_SHM_FORMAT_ARGB8888

static const struct zext_cursor_spy_manager_v1_interface cursor_spy_manager_impl;
static const struct zext_cursor_spy_v1_interface cursor_spy_impl;

static struct wlr_cursor_spy_manager_v1 *cursor_spy_manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zext_cursor_spy_manager_v1_interface,
			&cursor_spy_manager_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_cursor_spy_v1 *cursor_spy_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zext_cursor_spy_v1_interface, &cursor_spy_impl));
	return wl_resource_get_user_data(resource);
}

static void cursor_spy_destroy(struct wlr_cursor_spy_v1 *spy)
{
	if (!spy) {
		return;
	}

	wl_list_remove(&spy->output_destroy.link);
	wl_resource_set_user_data(spy->resource, NULL);
	free(spy);
}

static void cursor_spy_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_cursor_spy_v1 *spy = cursor_spy_from_resource(resource);
	cursor_spy_destroy(spy);
}

static void handle_cursor_spy_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

// Cursor image are stacked on top of each other
static void calculate_buffer_dimensions(struct wlr_output *output,
		uint32_t *width, uint32_t *height) {
	*width = 0;
	*height = 0;

	struct wlr_output_cursor *cursor;
	wl_list_for_each(cursor, &output->cursors, link) {
		if (!cursor->enabled || !cursor->visible) {
			continue;
		}

		if (cursor->width > *width) {
			*width = cursor->width;
		}

		*height += cursor->height;
	}
}

static bool send_buffer_info(struct wl_client *client,
		struct wlr_cursor_spy_v1 *spy, uint32_t width,
		uint32_t height) {
	enum wl_shm_format format = PREFERRED_FORMAT;
	zext_cursor_spy_v1_send_buffer(spy->resource, format, width, height);
	return true;
}

static void emit_cursor_event(struct wl_client *client,
		struct wlr_cursor_spy_v1 *spy, struct wl_shm_buffer *buffer,
		struct wlr_output_cursor *cursor, uint32_t *y_index) {
	uint32_t y = *y_index;
	*y_index += cursor->height;

	uint8_t *scratchpad = calloc(1, cursor->width * cursor->height * 4);
	if (!scratchpad) {
		wl_client_post_no_memory(client);
		return;
	}

	// TODO: This assert fails
	//assert(!(cursor->texture && cursor->surface));

	struct wlr_texture *texture = NULL;
	if (cursor->surface) {
		texture = wlr_surface_get_texture(cursor->surface);
	} else if (cursor->texture) {
		texture = cursor->texture;
	} else {
		abort();
	}

	uint8_t *pixels = wl_shm_buffer_get_data(buffer);
	uint32_t stride = wl_shm_buffer_get_stride(buffer);

	// TODO: Convert to preferred format if the formats don't match
	wlr_texture_read_pixels(texture, scratchpad);

	for (uint32_t row = 0; row < cursor->height; ++row) {
		memcpy(&pixels[(row + y) * stride],
				&scratchpad[row * cursor->width * 4],
				cursor->width * 4);
	}

	zext_cursor_spy_v1_send_cursor(spy->resource, y * stride, cursor->width,
			cursor->height, cursor->hotspot_x, cursor->hotspot_y);
}

static void handle_cursor_spy_spy(struct wl_client *client,
		struct wl_resource *spy_resource,
		struct wl_resource *buffer_resource, uint32_t seq) {
	struct wlr_cursor_spy_v1 *spy = cursor_spy_from_resource(spy_resource);
	struct wl_shm_buffer *buffer = wl_shm_buffer_get(buffer_resource);

	if (!spy) {
		return;
	}

	if (!buffer) {
		zext_cursor_spy_v1_send_failed(spy->resource,
				ZEXT_CURSOR_SPY_V1_ERROR_INVALID_BUFFER);
		return;
	}

	struct wlr_output *output = spy->output;

	if (seq == output->cursor_commit_seq) {
		goto done;
	}

	uint32_t min_width, min_height;
	calculate_buffer_dimensions(spy->output, &min_width, &min_height);

	uint32_t width = wl_shm_buffer_get_width(buffer);
	uint32_t height = wl_shm_buffer_get_height(buffer);
	uint32_t stride = wl_shm_buffer_get_stride(buffer);
	enum wl_shm_format format = wl_shm_buffer_get_format(buffer);

	if (width < min_width || height < min_height ||
			format != PREFERRED_FORMAT || stride < width * 4) {
		send_buffer_info(client, spy, min_width, min_height);
		return;
	}

	uint32_t y_index = 0;
	struct wlr_output_cursor *cursor;
	wl_list_for_each(cursor, &output->cursors, link) {
		if (cursor->enabled && cursor->visible) {
			emit_cursor_event(client, spy, buffer, cursor,
					&y_index);
		}
	}

done:
	zext_cursor_spy_v1_send_done(spy->resource, output->cursor_commit_seq);
}

static const struct zext_cursor_spy_v1_interface cursor_spy_impl = {
	.spy = handle_cursor_spy_spy,
	.destroy = handle_cursor_spy_destroy,
};

static void spy_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_cursor_spy_v1 *spy =
		wl_container_of(listener, spy, output_destroy);
	zext_cursor_spy_v1_send_failed(spy->resource,
			ZEXT_CURSOR_SPY_V1_ERROR_INVALID_OUTPUT);
	wl_resource_set_user_data(spy->resource, NULL);
	free(spy);
}

static void create_spy(struct wl_client *client,
		struct wlr_cursor_spy_manager_v1 *manager, uint32_t id,
		struct wlr_output *output) {
	struct wlr_cursor_spy_v1 *spy =
		calloc(1, sizeof(struct wlr_cursor_spy_v1));
	if (!spy) {
		wl_client_post_no_memory(client);
		return;
	}

	spy->output = output;
	spy->resource = wl_resource_create(client,
			&zext_cursor_spy_v1_interface,
			CURSOR_SPY_MANAGER_VERSION, id);
	if (!spy->resource) {
		wl_client_post_no_memory(client);
		goto failure;
	}

	wl_resource_set_implementation(spy->resource, &cursor_spy_impl,
			spy, cursor_spy_handle_resource_destroy);

	if (!output || !output->enabled) {
		zext_cursor_spy_v1_send_failed(spy->resource,
				ZEXT_CURSOR_SPY_V1_ERROR_INVALID_OUTPUT);
		goto output_failure;
	}

	uint32_t width, height;
	calculate_buffer_dimensions(output, &width, &height);
	if (!send_buffer_info(client, spy, width, height)) {
		zext_cursor_spy_v1_send_failed(spy->resource,
				ZEXT_CURSOR_SPY_V1_ERROR_INVALID_OUTPUT);
		goto buffer_failure;
	}

	wl_signal_add(&output->events.destroy, &spy->output_destroy);
	spy->output_destroy.notify = spy_handle_output_destroy;

	return;
buffer_failure:
output_failure:
	wl_resource_set_user_data(spy->resource, NULL);
failure:
	free(spy);
}

static void manager_handle_create_spy(struct wl_client *wl_client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *output_resource) {
	struct wlr_cursor_spy_manager_v1 *manager =
		cursor_spy_manager_from_resource(manager_resource);
	struct wlr_output *output = wlr_output_from_resource(output_resource);
	create_spy(wl_client, manager, id, output);
};

static void manager_handle_destroy(struct wl_client *wl_client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zext_cursor_spy_manager_v1_interface
		cursor_spy_manager_impl = {
	.create_spy = manager_handle_create_spy,
	.destroy = manager_handle_destroy,
};

static void manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_cursor_spy_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(wl_client,
		&zext_cursor_spy_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_resource_set_implementation(resource, &cursor_spy_manager_impl,
			manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_cursor_spy_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_cursor_spy_manager_v1 *wlr_cursor_spy_manager_v1_create(
		struct wl_display *display) {
	struct wlr_cursor_spy_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_cursor_spy_manager_v1));
	if (manager == NULL) {
		return NULL;
	}

	manager->global = wl_global_create(display,
		&zext_cursor_spy_manager_v1_interface,
		CURSOR_SPY_MANAGER_VERSION, manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
