#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>

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

static const float transforms[][4] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = {
		1.0f, 0.0f,
		0.0f, -1.0f,
	},
	[WL_OUTPUT_TRANSFORM_90] = {
		0.0f, -1.0f,
		-1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_180] = {
		-1.0f, 0.0f,
		0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_270] = {
		0.0f, 1.0f,
		1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED] = {
		-1.0f, 0.0f,
		0.0f, -1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = {
		0.0f, 1.0f,
		-1.0f, 0.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = {
		1.0f, 0.0f,
		0.0f, 1.0f,
	},
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = {
		0.0f, -1.0f,
		1.0f, 0.0f,
	},
};

// Equivilent to glOrtho(0, width, 0, height, 1, -1) with the transform applied
static void set_matrix(float mat[static 16], int32_t width, int32_t height,
		enum wl_output_transform transform) {
	memset(mat, 0, sizeof(*mat) * 16);

	const float *t = transforms[transform];
	float x = 2.0f / width;
	float y = 2.0f / height;

	// Rotation + relection
	mat[0] = x * t[0];
	mat[1] = x * t[1];
	mat[4] = y * t[2];
	mat[5] = y * t[3];

	// Translation
	mat[3] = -copysign(1.0f, mat[0] + mat[1]);
	mat[7] = -copysign(1.0f, mat[4] + mat[5]);

	// Identity
	mat[10] = 1.0f;
	mat[15] = 1.0f;
}

void wlr_output_update_matrix(struct wlr_output *output) {
	set_matrix(output->transform_matrix, output->width, output->height, output->transform);
}

struct wlr_output *wlr_output_create(struct wlr_output_impl *impl,
		struct wlr_output_state *state) {
	struct wlr_output *output = calloc(1, sizeof(struct wlr_output));
	output->impl = impl;
	output->state = state;
	output->modes = list_create();
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.resolution);
	return output;
}

void wlr_output_enable(struct wlr_output *output, bool enable) {
	output->impl->enable(output->state, enable);
}

bool wlr_output_set_mode(struct wlr_output *output, struct wlr_output_mode *mode) {
	if (!output->impl || !output->impl->set_mode) {
		return false;
	}
	bool result = output->impl->set_mode(output->state, mode);
	if (result) {
		wlr_output_update_matrix(output);
	}
	return result;
}

void wlr_output_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	wlr_output_update_matrix(output);
	output->impl->transform(output->state, transform);
}

bool wlr_output_set_cursor(struct wlr_output *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height) {
	if (!output->impl || !output->impl->set_cursor) {
		return false;
	}
	return output->impl->set_cursor(output->state, buf, stride, width, height);
}

bool wlr_output_move_cursor(struct wlr_output *output, int x, int y) {
	if (!output->impl || !output->impl->move_cursor) {
		return false;
	}
	return output->impl->move_cursor(output->state, x, y);
}

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) return;
	output->impl->destroy(output->state);
	for (size_t i = 0; output->modes && i < output->modes->length; ++i) {
		free(output->modes->items[i]);
	}
	list_free(output->modes);
	free(output);
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
