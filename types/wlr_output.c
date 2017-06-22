#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/list.h>

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
