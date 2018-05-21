#ifndef WLR_TYPES_WLR_OUTPUT_H
#define WLR_TYPES_WLR_OUTPUT_H

#include <pixman.h>
#include <stdbool.h>
#include <time.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/types/wlr_linux_dmabuf.h>

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

	struct wl_global *wl_global;
	struct wl_list wl_resources;

	char name[24];
	char make[48];
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
		struct wl_signal frame;
		struct wl_signal needs_swap;
		struct wl_signal swap_buffers;
		struct wl_signal enable;
		struct wl_signal mode;
		struct wl_signal scale;
		struct wl_signal transform;
		struct wl_signal destroy;
	} events;

	struct wl_event_source *idle_frame;

	struct wlr_surface *fullscreen_surface;
	struct wl_listener fullscreen_surface_commit;
	struct wl_listener fullscreen_surface_destroy;
	int fullscreen_width, fullscreen_height;

	struct wl_list cursors; // wlr_output_cursor::link
	struct wlr_output_cursor *hardware_cursor;

	// the output position in layout space reported to clients
	int32_t lx, ly;

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_surface;

void wlr_output_enable(struct wlr_output *output, bool enable);
void wlr_output_create_global(struct wlr_output *output);
void wlr_output_destroy_global(struct wlr_output *output);
/**
 * Sets the output mode.
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
 * Swaps the output buffers. If the time of the frame isn't known, set `when` to
 * NULL. If the compositor doesn't support damage tracking, set `damage` to
 * NULL.
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
void wlr_output_set_gamma(struct wlr_output *output,
	uint32_t size, uint16_t *r, uint16_t *g, uint16_t *b);
uint32_t wlr_output_get_gamma_size(struct wlr_output *output);
bool wlr_output_export_dmabuf(struct wlr_output *output,
	struct wlr_dmabuf_buffer_attribs *attribs);
void wlr_output_set_fullscreen_surface(struct wlr_output *output,
	struct wlr_surface *surface);
struct wlr_output *wlr_output_from_resource(struct wl_resource *resource);


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
