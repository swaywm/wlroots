/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_H
#define WLR_TYPES_WLR_OUTPUT_H

#include <stdbool.h>
#include <time.h>

#include <pixman.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include <wlr/render/allocator.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/format_set.h>

/*
 * This specifies how the backend should handle a mismatch between the
 * image size and the actual output size. This is merely a hint, and the
 * backend is free to ignore it.
 *
 * This is directly compatible with zwp_fullscreen_shell_v1.present_method.
 */
enum wlr_present_method {
	// No preference
	WLR_PRESENT_METHOD_DEFAULT = 0,
	// Center the view on the output
	WLR_PRESENT_METHOD_CENTER = 1,
	// Scale the surface, preserving aspect ratio, to the largest size that
	// will fit on the output
	WLR_PRESENT_METHOD_ZOOM = 2,
	// Scale the surface, preserving aspect ratio, to fully fit the output
	// cropping if needed
	WLR_PRESENT_METHOD_ZOOM_CROP = 3,
	// Scale the surface to the size of the output ignoring aspect ratio
	WLR_PRESENT_METHOD_STRETCH = 4,
};

struct wlr_output_mode {
	uint32_t flags; // enum wl_output_mode
	int32_t width, height;
	int32_t refresh; // mHz
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

struct wlr_output_impl;

/**
 * A compositor output region. This typically corresponds to a monitor that
 * displays part of the compositor space.
 *
 * Compositors should listen to the `frame` event to render an output. They
 * should call `wlr_output_make_current`, render and then call
 * `wlr_output_swap_buffers`. No rendering should happen outside a `frame` event
 * handler.
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
	struct wl_list modes;
	struct wlr_output_mode *current_mode;
	int32_t width, height;
	int32_t refresh; // mHz, may be zero

	bool enabled;
	float scale;
	enum wl_output_subpixel subpixel;
	enum wl_output_transform transform;

	bool needs_swap;
	// damage for cursors and fullscreen surface, in output-local coordinates
	pixman_region32_t damage;
	bool frame_pending;
	float transform_matrix[9];

	struct {
		// Request to render a frame
		struct wl_signal frame;
		// Emitted when buffers need to be swapped (because software cursors or
		// fullscreen damage or because of backend-specific logic)
		struct wl_signal needs_swap;
		// Emitted right before buffer swap
		struct wl_signal swap_buffers; // wlr_output_event_swap_buffers
		// Emitted right after the buffer has been presented to the user
		struct wl_signal present; // wlr_output_event_present
		struct wl_signal enable;
		struct wl_signal mode;
		struct wl_signal scale;
		struct wl_signal transform;
		struct wl_signal destroy;
	} events;

	struct wl_event_source *idle_frame;

	struct wl_list cursors; // wlr_output_cursor::link
	struct wlr_output_cursor *hardware_cursor;
	int software_cursor_locks; // number of locks forcing software cursors

	// the output position in layout space reported to clients
	int32_t lx, ly;

	// TODO: Remove this hack flag when the old interface is removed
	bool using_present;
	// TODO: Remove _2 suffix once other other damage member is removed
	pixman_region32_t damage_2;
	enum wlr_present_method present_method;
	struct wlr_image *image;

	struct {
		double src_x;
		double src_y;
		double src_w;
		double src_h;
		int32_t dest_w;
		int32_t dest_h;
	} viewport;

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_output_event_swap_buffers {
	struct wlr_output *output;
	struct timespec *when;
	pixman_region32_t *damage; // output-buffer-local coordinates
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
void wlr_output_set_position(struct wlr_output *output, int32_t lx, int32_t ly);
void wlr_output_set_scale(struct wlr_output *output, float scale);
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
 * Makes the output rendering context current.
 *
 * `buffer_age` is set to the drawing buffer age in number of frames or -1 if
 * unknown. This is useful for damage tracking.
 */
bool wlr_output_make_current(struct wlr_output *output, int *buffer_age);
/**
 * Get the preferred format for reading pixels.
 * This function might change the current rendering context.
 */
bool wlr_output_preferred_read_format(struct wlr_output *output,
	enum wl_shm_format *fmt);
/**
 * Swaps the output buffers. If the time of the frame isn't known, set `when` to
 * NULL. If the compositor doesn't support damage tracking, set `damage` to
 * NULL.
 *
 * Damage is given in output-buffer-local coordinates (ie. scaled and
 * transformed).
 *
 * Swapping buffers schedules a `frame` event.
 */
bool wlr_output_swap_buffers(struct wlr_output *output, struct timespec *when,
	pixman_region32_t *damage);
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

const struct wlr_format_set *wlr_output_get_formats(struct wlr_output *output);

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

/*
 * Presentation functions
 */

/*
 * Sets the rectangle of the input image which will be output to the display,
 * and the size of the destination rectangle.
 *
 * This is designed to match the wp_viewporter protocol interface.
 *
 * src_x, src_y, src_w and src_h control cropping.
 * dest_w and dest_h control scaling.
 *
 * This is not applied until the next call to wlr_output_present.
 *
 * Not every backend supports this.
 * TODO: Add a way to query if it's supported ahead of time.
 */
void wlr_output_set_viewport(struct wlr_output *output,
		double src_x, double src_y, double src_w, double src_h,
		int32_t dest_w, int32_t dest_h);

/*
 * Sets the way the backend handles images which are not the same size as its
 * output.
 *
 * This is designed to match the zwp_fullscreen_shell_v1 interface.
 *
 * This is not applied until the next call to wlr_output_present.
 *
 * See the wlr_present_method enum for more information.
 */
void wlr_output_set_present_method(struct wlr_output *output,
		enum wlr_present_method method);

/*
 * Sets the regions within the image which have changed.
 * This is in buffer coordinates.
 *
 * If region is NULL, the entire buffer is damaged.
 *
 * Some backends may ignore this value.
 */
void wlr_output_set_damage(struct wlr_output *output,
		pixman_region32_t *region);

/*
 * Sets the content to be displayed on the screen.
 * This image must have been previous created by or attached to this output's
 * backend, and must be a supported format.
 *
 * If img is NULL, the output's content is cleared.
 *
 * This is not applied until the next call to wlr_output_present.
 */
void wlr_output_set_image(struct wlr_output *output, struct wlr_image *img);

/*
 * Present the content to the screen.
 *
 * Upon calling this, all pending presentation state is reset, whether the
 * function succeeds or not.
 */
bool wlr_output_present(struct wlr_output *output);

#endif
