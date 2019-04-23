#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_output.h>
#include "util/signal.h"

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_destroy);
	wlr_output_damage_destroy(output_damage);
}

static void output_handle_mode(struct wl_listener *listener, void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_mode);
	wlr_output_damage_add_whole(output_damage);
}

static void output_handle_transform(struct wl_listener *listener, void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_transform);
	wlr_output_damage_add_whole(output_damage);
}

static void output_handle_scale(struct wl_listener *listener, void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_scale);
	wlr_output_damage_add_whole(output_damage);
}

static void output_handle_needs_frame(struct wl_listener *listener,
		void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_needs_frame);
	pixman_region32_union(&output_damage->current, &output_damage->current,
		&output_damage->output->damage);
	wlr_output_schedule_frame(output_damage->output);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_frame);

	if (!output_damage->output->enabled) {
		return;
	}

	wlr_signal_emit_safe(&output_damage->events.frame, output_damage);
}

static void output_handle_commit(struct wl_listener *listener, void *data) {
	struct wlr_output_damage *output_damage =
		wl_container_of(listener, output_damage, output_commit);

	if (!(output_damage->output->pending.committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}

	// same as decrementing, but works on unsigned integers
	output_damage->previous_idx += WLR_OUTPUT_DAMAGE_PREVIOUS_LEN - 1;
	output_damage->previous_idx %= WLR_OUTPUT_DAMAGE_PREVIOUS_LEN;

	pixman_region32_copy(&output_damage->previous[output_damage->previous_idx],
		&output_damage->current);
	pixman_region32_clear(&output_damage->current);
}

struct wlr_output_damage *wlr_output_damage_create(struct wlr_output *output) {
	struct wlr_output_damage *output_damage =
		calloc(1, sizeof(struct wlr_output_damage));
	if (output_damage == NULL) {
		return NULL;
	}

	output_damage->output = output;
	output_damage->max_rects = 20;
	wl_signal_init(&output_damage->events.frame);
	wl_signal_init(&output_damage->events.destroy);

	pixman_region32_init(&output_damage->current);
	for (size_t i = 0; i < WLR_OUTPUT_DAMAGE_PREVIOUS_LEN; ++i) {
		pixman_region32_init(&output_damage->previous[i]);
	}

	wl_signal_add(&output->events.destroy, &output_damage->output_destroy);
	output_damage->output_destroy.notify = output_handle_destroy;
	wl_signal_add(&output->events.mode, &output_damage->output_mode);
	output_damage->output_mode.notify = output_handle_mode;
	wl_signal_add(&output->events.transform, &output_damage->output_transform);
	output_damage->output_transform.notify = output_handle_transform;
	wl_signal_add(&output->events.scale, &output_damage->output_scale);
	output_damage->output_scale.notify = output_handle_scale;
	wl_signal_add(&output->events.needs_frame, &output_damage->output_needs_frame);
	output_damage->output_needs_frame.notify = output_handle_needs_frame;
	wl_signal_add(&output->events.frame, &output_damage->output_frame);
	output_damage->output_frame.notify = output_handle_frame;
	wl_signal_add(&output->events.commit, &output_damage->output_commit);
	output_damage->output_commit.notify = output_handle_commit;

	return output_damage;
}

void wlr_output_damage_destroy(struct wlr_output_damage *output_damage) {
	if (output_damage == NULL) {
		return;
	}
	wlr_signal_emit_safe(&output_damage->events.destroy, output_damage);
	wl_list_remove(&output_damage->output_destroy.link);
	wl_list_remove(&output_damage->output_mode.link);
	wl_list_remove(&output_damage->output_transform.link);
	wl_list_remove(&output_damage->output_scale.link);
	wl_list_remove(&output_damage->output_needs_frame.link);
	wl_list_remove(&output_damage->output_frame.link);
	pixman_region32_fini(&output_damage->current);
	for (size_t i = 0; i < WLR_OUTPUT_DAMAGE_PREVIOUS_LEN; ++i) {
		pixman_region32_fini(&output_damage->previous[i]);
	}
	free(output_damage);
}

bool wlr_output_damage_attach_render(struct wlr_output_damage *output_damage,
		bool *needs_frame, pixman_region32_t *damage) {
	struct wlr_output *output = output_damage->output;

	int buffer_age = -1;
	if (!wlr_output_attach_render(output, &buffer_age)) {
		return false;
	}

	// Check if we can use damage tracking
	if (buffer_age <= 0 || buffer_age - 1 > WLR_OUTPUT_DAMAGE_PREVIOUS_LEN) {
		int width, height;
		wlr_output_transformed_resolution(output, &width, &height);

		// Buffer new or too old, damage the whole output
		pixman_region32_union_rect(damage, damage, 0, 0, width, height);
	} else {
		pixman_region32_copy(damage, &output_damage->current);

		// Accumulate damage from old buffers
		size_t idx = output_damage->previous_idx;
		for (int i = 0; i < buffer_age - 1; ++i) {
			int j = (idx + i) % WLR_OUTPUT_DAMAGE_PREVIOUS_LEN;
			pixman_region32_union(damage, damage, &output_damage->previous[j]);
		}

		// Check the number of rectangles
		int n_rects = pixman_region32_n_rects(damage);
		if (n_rects > output_damage->max_rects) {
			pixman_box32_t *extents = pixman_region32_extents(damage);
			pixman_region32_union_rect(damage, damage, extents->x1, extents->y1,
				extents->x2 - extents->x1, extents->y2 - extents->y1);
		}
	}

	*needs_frame = output->needs_frame || pixman_region32_not_empty(damage);
	return true;
}

void wlr_output_damage_add(struct wlr_output_damage *output_damage,
		pixman_region32_t *damage) {
	int width, height;
	wlr_output_transformed_resolution(output_damage->output, &width, &height);

	pixman_region32_union(&output_damage->current, &output_damage->current,
		damage);
	pixman_region32_intersect_rect(&output_damage->current,
		&output_damage->current, 0, 0, width, height);
	wlr_output_schedule_frame(output_damage->output);
}

void wlr_output_damage_add_whole(struct wlr_output_damage *output_damage) {
	int width, height;
	wlr_output_transformed_resolution(output_damage->output, &width, &height);

	pixman_region32_union_rect(&output_damage->current, &output_damage->current,
		0, 0, width, height);

	wlr_output_schedule_frame(output_damage->output);
}

void wlr_output_damage_add_box(struct wlr_output_damage *output_damage,
		struct wlr_box *box) {
	int width, height;
	wlr_output_transformed_resolution(output_damage->output, &width, &height);

	pixman_region32_union_rect(&output_damage->current, &output_damage->current,
		box->x, box->y, box->width, box->height);
	pixman_region32_intersect_rect(&output_damage->current,
		&output_damage->current, 0, 0, width, height);
	wlr_output_schedule_frame(output_damage->output);
}
