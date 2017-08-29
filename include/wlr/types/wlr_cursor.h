#ifndef _WLR_TYPES_CURSOR_H
#define _WLR_TYPES_CURSOR_H
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_geometry.h>
#include <wlr/xcursor.h>

struct wlr_cursor_state;

struct wlr_cursor {
	struct wlr_cursor_state *state;
	int x, y;

	struct {
		struct wl_signal motion;
		struct wl_signal motion_absolute;
		struct wl_signal button;
		struct wl_signal axis;

		struct wl_signal touch_up;
		struct wl_signal touch_down;
		struct wl_signal touch_motion;
		struct wl_signal touch_cancel;

		struct wl_signal tablet_tool_axis;
		struct wl_signal tablet_tool_proximity;
		struct wl_signal tablet_tool_tip;
		struct wl_signal tablet_tool_button;
	} events;
};

struct wlr_cursor *wlr_cursor_init();

void wlr_cursor_destroy(struct wlr_cursor *cur);

void wlr_cursor_set_xcursor(struct wlr_cursor *cur, struct wlr_xcursor *xcur);

/**
 * Warp the cursor to the given x and y in layout coordinates. If x and y are
 * out of the layout boundaries or constraints, no warp will happen.
 *
 * `dev` may be passed to respect device mapping constraints. If `dev` is NULL,
 * device mapping constraints will be ignored.
 *
 * Returns true when the mouse warp was successful.
 */
bool wlr_cursor_warp(struct wlr_cursor *cur, struct wlr_input_device *dev,
		double x, double y);

void wlr_cursor_warp_absolute(struct wlr_cursor *cur,
		struct wlr_input_device *dev, double x_mm, double y_mm);

/**
 * Move the cursor in the direction of the given x and y coordinates.
 *
 * `dev` may be passed to respect device mapping constraints. If `dev` is NULL,
 * device mapping constraints will be ignored.
 */
void wlr_cursor_move(struct wlr_cursor *cur, struct wlr_input_device *dev,
		double delta_x, double delta_y);

/**
 * Attaches this input device to this cursor. The input device must be one of:
 *
 * - WLR_INPUT_DEVICE_POINTER
 * - WLR_INPUT_DEVICE_TOUCH
 * - WLR_INPUT_DEVICE_TABLET_TOOL
 */
void wlr_cursor_attach_input_device(struct wlr_cursor *cur,
		struct wlr_input_device *dev);

void wlr_cursor_detach_input_device(struct wlr_cursor *cur,
		struct wlr_input_device *dev);
/**
 * Uses the given layout to establish the boundaries and movement semantics of
 * this cursor. Cursors without an output layout allow infinite movement in any
 * direction and do not support absolute input events.
 */
void wlr_cursor_attach_output_layout(struct wlr_cursor *cur,
		struct wlr_output_layout *l);

/**
 * Attaches this cursor to the given output, which must be among the outputs in
 * the current output_layout for this cursor. This call is invalid for a cursor
 * without an associated output layout.
 */
void wlr_cursor_map_to_output(struct wlr_cursor *cur,
		struct wlr_output *output);

/**
 * Maps all input from a specific input device to a given output. The input
 * device must be attached to this cursor and the output must be among the
 * outputs in the attached output layout.
 */
void wlr_cursor_map_input_to_output(struct wlr_cursor *cur,
		struct wlr_input_device *dev, struct wlr_output *output);

/**
 * Maps this cursor to an arbitrary region on the associated wlr_output_layout.
 */
void wlr_cursor_map_to_region(struct wlr_cursor *cur, struct wlr_geometry *geo);

/**
 * Maps inputs from this input device to an arbitrary region on the associated
 * wlr_output_layout.
 */
void wlr_cursor_map_input_to_region(struct wlr_cursor *cur,
		struct wlr_input_device *dev, struct wlr_geometry *geo);

#endif
