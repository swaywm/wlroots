/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_H
#define WLR_TYPES_WLR_OUTPUT_H

#include <pixman.h>
#include <stdbool.h>
#include <time.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/dmabuf.h>
#include <wlr/types/wlr_buffer.h>

struct wlr_output_mode {
	int32_t width, height;
	int32_t refresh; // mHz
	bool preferred;
	struct wl_list link;
};

struct wlr_output_cursor {
	struct wlr_output *output;
	double x, y;
	bool enabled;
	bool visible;
	uint32_t width, height;
	int32_t hotspot_x, hotspot_y;
	struct wl_list link;

	// only when using a software cursor without a surface
	struct wlr_texture *texture;

	// only when using a cursor surface
	struct wlr_surface *surface;
	struct wl_listener surface_commit;
	struct wl_listener surface_destroy;

	struct {
		struct wl_signal destroy;
	} events;
};

enum wlr_output_state_field {
	WLR_OUTPUT_STATE_BUFFER = 1 << 0,
	WLR_OUTPUT_STATE_DAMAGE = 1 << 1,
};

enum wlr_output_state_buffer_type {
	WLR_OUTPUT_STATE_BUFFER_RENDER,
	WLR_OUTPUT_STATE_BUFFER_SCANOUT,
};

/**
 * Holds the double-buffered output state.
 */
struct wlr_output_state {
	uint32_t committed; // enum wlr_output_state_field
	pixman_region32_t damage; // output-buffer-local coordinates

	// only valid if WLR_OUTPUT_STATE_BUFFER
	enum wlr_output_state_buffer_type buffer_type;
	struct wlr_buffer *buffer; // if WLR_OUTPUT_STATE_BUFFER_SCANOUT
};

struct wlr_output_impl;

/**
 * A compositor output region. This typically corresponds to a monitor that
 * displays part of the compositor space.
 *
 * The `frame` event will be emitted when it is a good time for the compositor
 * to submit a new frame.
 *
 * To render a new frame, compositors should call `wlr_output_attach_render`,
 * render and call `wlr_output_commit`. No rendering should happen outside a
 * `frame` event handler or before `wlr_output_attach_render`.
 */
struct wlr_output {
	const struct wlr_output_impl *impl;
	struct wlr_backend *backend;
	struct wl_display *display;

	struct wl_global *global;
	struct wl_list resources;

	char name[24];
	char make[56];
	char model[16];
	char serial[16];
	int32_t phys_width, phys_height; // mm

	// Note: some backends may have zero modes
	struct wl_list modes; // wlr_output_mode::link
	struct wlr_output_mode *current_mode;
	int32_t width, height;
	int32_t refresh; // mHz, may be zero

	bool enabled;
	float scale;
	enum wl_output_subpixel subpixel;
	enum wl_output_transform transform;

	bool needs_frame;
	// damage for cursors and fullscreen surface, in output-local coordinates
	pixman_region32_t damage;
	bool frame_pending;
	float transform_matrix[9];

	struct wlr_output_state pending;

	// Commit sequence number. Incremented on each commit, may overflow.
	uint32_t commit_seq;

	struct {
		// Request to render a frame
		struct wl_signal frame;
		// Emitted when buffers need to be swapped (because software cursors or
		// fullscreen damage or because of backend-specific logic)
		struct wl_signal needs_frame;
		// Emitted right before commit
		struct wl_signal precommit; // wlr_output_event_precommit
		// Emitted right after commit
		struct wl_signal commit;
		// Emitted right after the buffer has been presented to the user
		struct wl_signal present; // wlr_output_event_present
		struct wl_signal enable;
		struct wl_signal mode;
		struct wl_signal scale;
		struct wl_signal transform;
		struct wl_signal destroy;
	} events;

	struct wl_event_source *idle_frame;
	struct wl_event_source *idle_done;

	int attach_render_locks; // number of locks forcing rendering

	struct wl_list cursors; // wlr_output_cursor::link
	struct wlr_output_cursor *hardware_cursor;
	int software_cursor_locks; // number of locks forcing software cursors

	struct wl_listener display_destroy;

	void *data;

	bool block_idle_frame;
};

struct wlr_output_event_precommit {
	struct wlr_output *output;
	struct timespec *when;
};

enum wlr_output_present_flag {
	// The presentation was synchronized to the "vertical retrace" by the
	// display hardware such that tearing does not happen.
	WLR_OUTPUT_PRESENT_VSYNC = 0x1,
	// The display hardware provided measurements that the hardware driver
	// converted into a presentation timestamp.
	WLR_OUTPUT_PRESENT_HW_CLOCK = 0x2,
	// The display hardware signalled that it started using the new image
	// content.
	WLR_OUTPUT_PRESENT_HW_COMPLETION = 0x4,
	// The presentation of this update was done zero-copy.
	WLR_OUTPUT_PRESENT_ZERO_COPY = 0x8,
};

struct wlr_output_event_present {
	struct wlr_output *output;
	// Frame submission for which this presentation event is for (see
	// wlr_output.commit_seq).
	uint32_t commit_seq;
	// Time when the content update turned into light the first time.
	struct timespec *when;
	// Vertical retrace counter. Zero if unavailable.
	unsigned seq;
	// Prediction of how many nanoseconds after `when` the very next output
	// refresh may occur. Zero if unknown.
	int refresh; // nsec
	uint32_t flags; // enum wlr_output_present_flag
};

struct wlr_surface;

/**
 * Enables or disables the output. A disabled output is turned off and doesn't
 * emit `frame` events.
 */
bool wlr_output_enable(struct wlr_output *output, bool enable);
void wlr_output_create_global(struct wlr_output *output);
void wlr_output_destroy_global(struct wlr_output *output);
/**
 * Returns the preferred mode for this output. If the output doesn't support
 * modes, returns NULL.
 */
struct wlr_output_mode *wlr_output_preferred_mode(struct wlr_output *output);
/**
 * Sets the output mode. Enables the output if it's currently disabled.
 */
bool wlr_output_set_mode(struct wlr_output *output,
	struct wlr_output_mode *mode);
/**
 * Sets a custom mode on the output. If modes are available, they are preferred.
 * Setting `refresh` to zero lets the backend pick a preferred value.
 */
bool wlr_output_set_custom_mode(struct wlr_output *output, int32_t width,
	int32_t height, int32_t refresh);
void wlr_output_set_transform(struct wlr_output *output,
	enum wl_output_transform transform);
void wlr_output_set_scale(struct wlr_output *output, float scale);
void wlr_output_set_subpixel(struct wlr_output *output,
	enum wl_output_subpixel subpixel);
/**
 * Schedule a done event.
 *
 * This is intended to be used by wl_output add-on interfaces.
 */
void wlr_output_schedule_done(struct wlr_output *output);
void wlr_output_destroy(struct wlr_output *output);
/**
 * Computes the transformed output resolution.
 */
void wlr_output_transformed_resolution(struct wlr_output *output,
	int *width, int *height);
/**
 * Computes the transformed and scaled output resolution.
 */
void wlr_output_effective_resolution(struct wlr_output *output,
	int *width, int *height);
/**
 * Attach the renderer's buffer to the output. Compositors must call this
 * function before rendering. After they are done rendering, they should call
 * `wlr_output_commit` to submit the new frame.
 *
 * If non-NULL, `buffer_age` is set to the drawing buffer age in number of
 * frames or -1 if unknown. This is useful for damage tracking.
 */
bool wlr_output_attach_render(struct wlr_output *output, int *buffer_age);
/**
 * Attach a buffer to the output. Compositors should call `wlr_output_commit`
 * to submit the new frame.
 */
bool wlr_output_attach_buffer(struct wlr_output *output,
	struct wlr_buffer *buffer);
/**
 * Get the preferred format for reading pixels.
 * This function might change the current rendering context.
 */
bool wlr_output_preferred_read_format(struct wlr_output *output,
	enum wl_shm_format *fmt);
/**
 * Set the damage region for the frame to be submitted. This is the region of
 * the screen that has changed since the last frame.
 *
 * Compositors implementing damage tracking should call this function with the
 * damaged region in output-buffer-local coordinates (ie. scaled and
 * transformed).
 *
 * This region is not to be confused with the renderer's buffer damage, ie. the
 * region compositors need to repaint. Compositors usually need to repaint more
 * than what changed since last frame since multiple render buffers are used.
 */
void wlr_output_set_damage(struct wlr_output *output,
	pixman_region32_t *damage);
/**
 * Commit the pending output state. If `wlr_output_attach_render` has been
 * called, the pending frame will be submitted for display.
 *
 * This function schedules a `frame` event.
 */
bool wlr_output_commit(struct wlr_output *output);
/**
 * Manually schedules a `frame` event. If a `frame` event is already pending,
 * it is a no-op.
 */
void wlr_output_schedule_frame(struct wlr_output *output);
/**
 * Returns the maximum length of each gamma ramp, or 0 if unsupported.
 */
size_t wlr_output_get_gamma_size(struct wlr_output *output);
/**
 * Sets the gamma table for this output. `r`, `g` and `b` are gamma ramps for
 * red, green and blue. `size` is the length of the ramps and must not exceed
 * the value returned by `wlr_output_get_gamma_size`.
 *
 * Providing zero-sized ramps resets the gamma table.
 */
bool wlr_output_set_gamma(struct wlr_output *output, size_t size,
	const uint16_t *r, const uint16_t *g, const uint16_t *b);
bool wlr_output_export_dmabuf(struct wlr_output *output,
	struct wlr_dmabuf_attributes *attribs);
struct wlr_output *wlr_output_from_resource(struct wl_resource *resource);
/**
 * Locks the output to only use rendering instead of direct scan-out. This is
 * useful if direct scan-out needs to be temporarily disabled (e.g. during
 * screen capture). There must be as many unlocks as there have been locks to
 * restore the original state. There should never be an unlock before a lock.
 */
void wlr_output_lock_attach_render(struct wlr_output *output, bool lock);
/**
 * Locks the output to only use software cursors instead of hardware cursors.
 * This is useful if hardware cursors need to be temporarily disabled (e.g.
 * during screen capture). There must be as many unlocks as there have been
 * locks to restore the original state. There should never be an unlock before
 * a lock.
 */
void wlr_output_lock_software_cursors(struct wlr_output *output, bool lock);
/**
 * Renders software cursors. This is a utility function that can be called when
 * compositors render.
 */
void wlr_output_render_software_cursors(struct wlr_output *output,
	pixman_region32_t *damage);


struct wlr_output_cursor *wlr_output_cursor_create(struct wlr_output *output);
/**
 * Sets the cursor image. The image must be already scaled for the output.
 */
bool wlr_output_cursor_set_image(struct wlr_output_cursor *cursor,
	const uint8_t *pixels, int32_t stride, uint32_t width, uint32_t height,
	int32_t hotspot_x, int32_t hotspot_y);
void wlr_output_cursor_set_surface(struct wlr_output_cursor *cursor,
	struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y);
bool wlr_output_cursor_move(struct wlr_output_cursor *cursor,
	double x, double y);
void wlr_output_cursor_destroy(struct wlr_output_cursor *cursor);


/**
 * Returns the transform that, when composed with `tr`, gives
 * `WL_OUTPUT_TRANSFORM_NORMAL`.
 */
enum wl_output_transform wlr_output_transform_invert(
	enum wl_output_transform tr);

/**
 * Returns a transform that, when applied, has the same effect as applying
 * sequentially `tr_a` and `tr_b`.
 */
enum wl_output_transform wlr_output_transform_compose(
	enum wl_output_transform tr_a, enum wl_output_transform tr_b);

#endif
