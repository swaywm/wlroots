/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_INTERFACES_WLR_OUTPUT_H
#define WLR_INTERFACES_WLR_OUTPUT_H

#include <stdbool.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>

/**
 * A backend implementation of wlr_output.
 *
 * The functions commit, attach_render and rollback_render are mandatory. Other
 * functions are optional.
 */
struct wlr_output_impl {
	/**
	 * Set the output cursor plane image.
	 *
	 * The parameters describe the image texture, its scale and its transform.
	 * If the scale and transform doesn't match the output's, the backend is
	 * responsible for scaling and transforming the texture appropriately.
	 * If texture is NULL, the cursor should be hidden.
	 *
	 * The hotspot indicates the offset that needs to be applied to the
	 * top-left corner of the image to match the cursor position. In other
	 * words, the image should be displayed at (x - hotspot_x, y - hotspot_y).
	 *
	 * If update_texture is true, all parameters need to be taken into account.
	 * If update_texture is false, only the hotspot is to be updated.
	 */
	bool (*set_cursor)(struct wlr_output *output, struct wlr_texture *texture,
		float scale, enum wl_output_transform transform,
		int32_t hotspot_x, int32_t hotspot_y, bool update_texture);
	/**
	 * Set the output cursor plane position.
	 *
	 * The position is relative to the cursor hotspot, see set_cursor.
	 */
	bool (*move_cursor)(struct wlr_output *output, int x, int y);
	/**
	 * Cleanup backend-specific resources tied to the output.
	 */
	void (*destroy)(struct wlr_output *output);
	/**
	 * Make the output's back-buffer current for the renderer.
	 *
	 * buffer_age must be set to the buffer age in number of frames, or -1 if
	 * unknown.
	 */
	bool (*attach_render)(struct wlr_output *output, int *buffer_age);
	/**
	 * Unset the current renderer's buffer.
	 *
	 * This is the opposite of attach_render.
	 */
	void (*rollback_render)(struct wlr_output *output);
	/**
	 * Check that the pending output state is a valid configuration.
	 *
	 * If this function returns true, commit can only fail due to a runtime
	 * error.
	 */
	bool (*test)(struct wlr_output *output);
	/**
	 * Commit the pending output state.
	 *
	 * If a buffer has been attached, a frame event is scheduled.
	 */
	bool (*commit)(struct wlr_output *output);
	/**
	 * Get the maximum number of gamma LUT elements for each channel.
	 *
	 * Zero can be returned if the output doesn't support gamma LUTs.
	 */
	size_t (*get_gamma_size)(struct wlr_output *output);
	/**
	 * Export the output's current back-buffer as a DMA-BUF.
	 */
	bool (*export_dmabuf)(struct wlr_output *output,
		struct wlr_dmabuf_attributes *attribs);
};

/**
 * Initialize a new output.
 */
void wlr_output_init(struct wlr_output *output, struct wlr_backend *backend,
	const struct wlr_output_impl *impl, struct wl_display *display);
/**
 * Update the current output mode.
 *
 * The backend must call this function when the mode is updated to notify
 * compositors about the change.
 */
void wlr_output_update_mode(struct wlr_output *output,
	struct wlr_output_mode *mode);
/**
 * Update the current output custom mode.
 *
 * The backend must call this function when the mode is updated to notify
 * compositors about the change.
 */
void wlr_output_update_custom_mode(struct wlr_output *output, int32_t width,
	int32_t height, int32_t refresh);
/**
 * Update the current output status.
 *
 * The backend must call this function when the status is updated to notify
 * compositors about the change.
 */
void wlr_output_update_enabled(struct wlr_output *output, bool enabled);
/**
 * Notify compositors that they need to submit a new frame in order to apply
 * output changes.
 */
void wlr_output_update_needs_frame(struct wlr_output *output);
/**
 * Notify compositors that the output needs to be fully repainted.
 */
void wlr_output_damage_whole(struct wlr_output *output);
/**
 * Send a frame event.
 *
 * See wlr_output.events.frame.
 */
void wlr_output_send_frame(struct wlr_output *output);
/**
 * Send a present event.
 *
 * See wlr_output.events.present.
 */
void wlr_output_send_present(struct wlr_output *output,
	struct wlr_output_event_present *event);

#endif
