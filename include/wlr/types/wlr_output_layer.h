/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_LAYER_H
#define WLR_TYPES_WLR_OUTPUT_LAYER_H

/**
 * Output layers provide an API to display buffers without rendering them. It
 * relies on backend features which are not always available: the backend can
 * refuse to display a layer. However when the backend accepts to display a
 * layer, performance and battery usage will be improved.
 *
 * Operations on output layers are double-buffered. An output commit is needed
 * to apply the pending state.
 */

#include <wayland-server-core.h>

enum wlr_output_layer_state_field {
	WLR_OUTPUT_LAYER_STATE_LINK = 1 << 0,
	WLR_OUTPUT_LAYER_STATE_BUFFER = 1 << 1,
	WLR_OUTPUT_LAYER_STATE_POSITION = 1 << 2,
};

struct wlr_output_layer_state {
	uint32_t committed; // enum wlr_output_layer_state_field
	struct wl_list link;
	struct wlr_buffer *buffer; // only valid if WLR_OUTPUT_LAYER_STATE_BUFFER
	int x, y; // only valid if WLR_OUTPUT_LAYER_STATE_POSITION
};

struct wlr_output_layer {
	struct wlr_output *output;
	/** If true, the backend has accepted to display the layer. If false, the
	 * compositor needs to manually render the layer. After each output commit,
	 * the backend will update this field. */
	bool accepted;

	struct wlr_output_layer_state current, pending;
};

/**
 * Create a new output layer.
 */
struct wlr_output_layer *wlr_output_layer_create(struct wlr_output *output);
/**
 * Remove the output layer. This operation is double-buffered, see
 * wlr_output_commit.
 *
 * Callers must not access the wlr_output_layer after calling this function.
 */
void wlr_output_layer_remove(struct wlr_output_layer *layer);
/**
 * Attach a buffer to the layer. This operation is double-buffered, see
 * wlr_output_commit.
 */
void wlr_output_layer_attach_buffer(struct wlr_output_layer *layer,
	struct wlr_buffer *buffer);
/**
 * Set the position of the layer relative to the output. The coordinates are
 * given in output-buffer-local coordinates. This operation is double-buffered,
 * see wlr_output_commit.
 */
void wlr_output_layer_move(struct wlr_output_layer *layer, int x, int y);
/**
 * Move the layer right above the specified sibling. This operation is
 * double-buffered, see wlr_output_commit.
 */
void wlr_output_layer_place_above(struct wlr_output_layer *layer,
	struct wlr_output_layer *sibling);
/**
 * Move the layer right below the specified sibling. This operation is
 * double-buffered, see wlr_output_commit.
 */
void wlr_output_layer_place_below(struct wlr_output_layer *layer,
	struct wlr_output_layer *sibling);

#endif
