#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include <GLES2/gl2.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>

static void wl_output_send_to_resource(struct wl_resource *resource) {
	assert(resource);
	struct wlr_output *output = wl_resource_get_user_data(resource);
	assert(output);
	const uint32_t version = wl_resource_get_version(resource);
	if (version >= WL_OUTPUT_GEOMETRY_SINCE_VERSION) {
		wl_output_send_geometry(resource, 0, 0, // TODO: get position from layout?
				output->phys_width, output->phys_height, output->subpixel,
				output->make, output->model, output->transform);
	}
	if (version >= WL_OUTPUT_MODE_SINCE_VERSION) {
		for (size_t i = 0; i < output->modes->length; ++i) {
			struct wlr_output_mode *mode = output->modes->items[i];
			// TODO: mode->flags should just be preferred
			uint32_t flags = mode->flags;
			if (output->current_mode == mode) {
				flags |= WL_OUTPUT_MODE_CURRENT;
			}
			wl_output_send_mode(resource, flags,
					mode->width, mode->height, mode->refresh);
		}
	}
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
		wl_output_send_scale(resource, output->scale);
	}
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(resource);
	}
}

static void wl_output_destroy(struct wl_resource *resource) {
	struct wlr_output *output = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &output->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_output_release(struct wl_client *client, struct wl_resource *resource) {
	wl_output_destroy(resource);
}

static struct wl_output_interface wl_output_impl = {
	.release = wl_output_release
};

static void wl_output_bind(struct wl_client *wl_client, void *_wlr_output,
		uint32_t version, uint32_t id) {
	struct wlr_output *wlr_output = _wlr_output;
	assert(wl_client && wlr_output);
	if (version > 3) {
		wlr_log(L_ERROR, "Client requested unsupported wl_output version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &wl_output_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_output_impl,
			wlr_output, wl_output_destroy);
	wl_list_insert(&wlr_output->wl_resources, wl_resource_get_link(wl_resource));
	wl_output_send_to_resource(wl_resource);
}

struct wl_global *wlr_output_create_global(
		struct wlr_output *wlr_output, struct wl_display *display) {
	struct wl_global *wl_global = wl_global_create(display,
		&wl_output_interface, 3, wlr_output, wl_output_bind);
	wlr_output->wl_global = wl_global;
	wl_list_init(&wlr_output->wl_resources);
	return wl_global;
}

void wlr_output_update_matrix(struct wlr_output *output) {
	wlr_matrix_texture(output->transform_matrix, output->width, output->height, output->transform);
}

void wlr_output_init(struct wlr_output *output,
		const struct wlr_output_impl *impl) {
	output->impl = impl;
	output->modes = list_create();
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.resolution);
}

void wlr_output_enable(struct wlr_output *output, bool enable) {
	output->impl->enable(output, enable);
}

bool wlr_output_set_mode(struct wlr_output *output, struct wlr_output_mode *mode) {
	if (!output->impl || !output->impl->set_mode) {
		return false;
	}
	bool result = output->impl->set_mode(output, mode);
	if (result) {
		wlr_output_update_matrix(output);
	}
	return result;
}

void wlr_output_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->impl->transform(output, transform);
	wlr_output_update_matrix(output);
}

bool wlr_output_set_cursor(struct wlr_output *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	if (output->impl->set_cursor
			&& output->impl->set_cursor(output, buf, stride, width, height)) {
		output->cursor.is_sw = false;
		return true;
	}

	wlr_log(L_INFO, "Falling back to software cursor");

	output->cursor.is_sw = true;
	output->cursor.width = width;
	output->cursor.height = height;

	if (!output->cursor.renderer) {
		/* NULL egl is okay given that we are only using pixel buffers */
		output->cursor.renderer = wlr_gles2_renderer_init(NULL);
		if (!output->cursor.renderer) {
			return false;
		}
	}

	if (!output->cursor.texture) {
		output->cursor.texture = wlr_render_texture_init(output->cursor.renderer);
		if (!output->cursor.texture) {
			return false;
		}
	}

	return wlr_texture_upload_pixels(output->cursor.texture,
				WL_SHM_FORMAT_ARGB8888, stride, width, height, buf);
}

bool wlr_output_move_cursor(struct wlr_output *output, int x, int y) {
	output->cursor.x = x;
	output->cursor.y = y;

	if (output->cursor.is_sw) {
		return true;
	}

	if (!output->impl->move_cursor) {
		return false;
	}

	return output->impl->move_cursor(output, x, y);
}

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) {
		return;
	}

	wlr_texture_destroy(output->cursor.texture);
	wlr_renderer_destroy(output->cursor.renderer);

	for (size_t i = 0; output->modes && i < output->modes->length; ++i) {
		struct wlr_output_mode *mode = output->modes->items[i];
		free(mode);
	}
	list_free(output->modes);
	if (output->impl && output->impl->destroy) {
		output->impl->destroy(output);
	} else {
		free(output);
	}
}

void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height) {
	// TODO: Scale factor
	if (output->transform % 2 == 1) {
		*width = output->height;
		*height = output->width;
	} else {
		*width = output->width;
		*height = output->height;
	}
}

void wlr_output_make_current(struct wlr_output *output) {
	output->impl->make_current(output);
}

void wlr_output_swap_buffers(struct wlr_output *output) {
	if (output->cursor.is_sw) {
		glViewport(0, 0, output->width, output->height);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		float matrix[16];
		wlr_texture_get_matrix(output->cursor.texture, &matrix, &output->transform_matrix,
			output->cursor.x, output->cursor.y);
		wlr_render_with_matrix(output->cursor.renderer, output->cursor.texture, &matrix);
	}

	output->impl->swap_buffers(output);
}
