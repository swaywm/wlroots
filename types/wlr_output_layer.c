#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layer.h>
#include "types/wlr_output_layer.h"

struct wlr_output_layer *wlr_output_layer_create(struct wlr_output *output) {
	if (output->impl->create_layer) {
		return output->impl->create_layer(output);
	}

	struct wlr_output_layer *layer = calloc(1, sizeof(*layer));
	if (layer == NULL) {
		return NULL;
	}
	wlr_output_layer_init(layer, output);
	return layer;
}

void wlr_output_layer_init(struct wlr_output_layer *layer,
		struct wlr_output *output) {
	layer->output = output;
	wl_list_insert(output->layers.prev, &layer->current.link);
	wl_list_insert(output->pending.layers.prev, &layer->pending.link);
	layer->pending.committed |= WLR_OUTPUT_LAYER_STATE_LINK;
	output->pending.committed |= WLR_OUTPUT_STATE_LAYERS;
}

void output_layer_destroy(struct wlr_output_layer *layer) {
	output_layer_state_clear(&layer->current);
	output_layer_state_clear(&layer->pending);

	wl_list_remove(&layer->current.link);
	wl_list_remove(&layer->pending.link);

	if (layer->output->impl->destroy_layer) {
		layer->output->impl->destroy_layer(layer);
	} else {
		free(layer);
	}
}

void wlr_output_layer_remove(struct wlr_output_layer *layer) {
	wlr_output_layer_attach_buffer(layer, NULL);

	wl_list_remove(&layer->pending.link);
	wl_list_init(&layer->pending.link);
	layer->pending.committed |= WLR_OUTPUT_LAYER_STATE_LINK;
	layer->output->pending.committed |= WLR_OUTPUT_STATE_LAYERS;
}

void wlr_output_layer_attach_buffer(struct wlr_output_layer *layer,
		struct wlr_buffer *buffer) {
	if (buffer == layer->current.buffer) {
		layer->pending.committed &= ~WLR_OUTPUT_LAYER_STATE_BUFFER;
		return;
	}

	wlr_buffer_unlock(layer->pending.buffer);
	layer->pending.buffer = NULL;

	if (buffer == NULL) {
		return;
	}

	layer->pending.buffer = wlr_buffer_lock(buffer);
	layer->pending.committed |= WLR_OUTPUT_LAYER_STATE_BUFFER;
}

void wlr_output_layer_move(struct wlr_output_layer *layer, int x, int y) {
	if (x == layer->current.x && y == layer->current.y) {
		layer->pending.committed &= ~WLR_OUTPUT_LAYER_STATE_POSITION;
		return;
	}

	layer->pending.x = x;
	layer->pending.y = y;
	layer->pending.committed |= WLR_OUTPUT_LAYER_STATE_POSITION;
}

void wlr_output_layer_place_above(struct wlr_output_layer *layer,
		struct wlr_output_layer *sibling) {
	assert(layer->output == sibling->output);

	wl_list_remove(&layer->pending.link);
	wl_list_insert(&sibling->pending.link, &layer->pending.link);

	if (layer->current.link.prev != &layer->output->layers) {
		struct wlr_output_layer *prev =
			wl_container_of(layer->current.link.prev, prev, current.link);
		if (prev == sibling) {
			return;
		}
	}

	layer->pending.committed |= WLR_OUTPUT_LAYER_STATE_LINK;
	layer->output->pending.committed |= WLR_OUTPUT_STATE_LAYERS;
}

void wlr_output_layer_place_below(struct wlr_output_layer *layer,
		struct wlr_output_layer *sibling) {
	assert(layer->output == sibling->output);

	wl_list_remove(&layer->pending.link);
	wl_list_insert(sibling->pending.link.prev, &layer->pending.link);

	if (layer->current.link.next != &layer->output->layers) {
		struct wlr_output_layer *next =
			wl_container_of(layer->current.link.next, next, current.link);
		if (next == sibling) {
			return;
		}
	}

	layer->pending.committed |= WLR_OUTPUT_LAYER_STATE_LINK;
	layer->output->pending.committed |= WLR_OUTPUT_STATE_LAYERS;
}

static void output_layer_state_copy(struct wlr_output_layer_state *dst,
		struct wlr_output_layer_state *src) {
	// link has already been taken care of
	if (src->committed & WLR_OUTPUT_LAYER_STATE_BUFFER) {
		wlr_buffer_unlock(dst->buffer);
		if (src->buffer != NULL) {
			dst->buffer = wlr_buffer_lock(src->buffer);
		} else {
			dst->buffer = NULL;
		}
	}
	if (src->committed & WLR_OUTPUT_LAYER_STATE_POSITION) {
		dst->x = src->x;
		dst->y = src->y;
	}
	dst->committed |= src->committed;
}

void output_layer_state_clear(struct wlr_output_layer_state *state) {
	wlr_buffer_unlock(state->buffer);
	state->buffer = NULL;
	state->committed = 0;
}

void output_layer_state_move(struct wlr_output_layer_state *dst,
		struct wlr_output_layer_state *src) {
	output_layer_state_copy(dst, src);
	output_layer_state_clear(src);
}
