#ifndef TYPES_WLR_OUTPUT_LAYER_H
#define TYPES_WLR_OUTPUT_LAYER_H

#include <wlr/types/wlr_output_layer.h>

void output_layer_destroy(struct wlr_output_layer *layer);
void output_layer_state_move(struct wlr_output_layer_state *dst,
	struct wlr_output_layer_state *src);
void output_layer_state_clear(struct wlr_output_layer_state *state);

#endif
