/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_INPUT_TIMESTAMPS_V1_H
#define WLR_TYPES_WLR_INPUT_TIMESTAMPS_V1_H

#include <wayland-server.h>

/**
 * This protocol specifies a way for a client to request and receive
 * high-resolution timestamps for input events.
 */

/**
 * A global interface used for requesting high-resolution timestamps for
 * input events.
 */
struct wlr_input_timestamps_manager_v1 {
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link()
	struct wl_list input_timestamps; // wlr_input_timestamps_v1::link

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener display_destroy_listener;

	void *data;
};

/**
 * Enum used to specify the type of input device to provide event timestamps
 * for.
 */
enum wlr_input_timestamps_type {
	WLR_INPUT_TIMESTAMPS_KEYBOARD,
	WLR_INPUT_TIMESTAMPS_POINTER,
	WLR_INPUT_TIMESTAMPS_TOUCH,
};

/**
 * Provides high-resolution timestamp events for a set of subscribed input
 * events. The set of subscribed input events is determined by the
 * zwp_input_timestamps_manager_v1 request used to create this object.
 */
struct wlr_input_timestamps_v1 {
	struct wl_resource *resource;
	struct wl_resource *input_resource;
	struct wlr_seat *seat;
	struct wl_list link; // wlr_input_timestamps_manager_v1::input_timestamps

	enum wlr_input_timestamps_type input_timestamps_type;

	struct {
		struct wl_signal destroy;
	} events;

	struct wl_listener seat_destroy;
	struct wl_listener input_destroy;

	void *data;
};

/**
 * Public creator and destructor functions.
 */
struct wlr_input_timestamps_manager_v1 *wlr_input_timestamps_manager_v1_create(
	struct wl_display *display);

void wlr_input_timestamps_manager_v1_destroy(
	struct wlr_input_timestamps_manager_v1 *manager);

/**
 * Public functions to send timestamps for a particular set of subscribed
 * events, depending on the type of input device.
 *
 * The timestamp event is associated with the first subsequent input event
 * carrying a timestamp which belongs to the set of input events this object is
 * subscribed to.
 *
 * The timestamp provided by this event is a high-resolution version of the
 * timestamp argument of the associated input event. The provided timestamp is
 * in the same clock domain and is at least as accurate as the associated input
 * event timestamp.
 *
 * The timestamp is expressed as tv_sec_hi, tv_sec_lo, tv_nsec triples, each
 * component being an unsigned 32-bit value. Whole seconds are in tv_sec which
 * is a 64-bit value combined from tv_sec_hi and tv_sec_lo, and the additional
 * fractional part in tv_nsec as nanoseconds. Hence, for valid timestamps
 * tv_nsec must be in [0, 999999999].
 */
void wlr_input_timestamps_manager_v1_send_keyboard_timestamp(
	struct wlr_input_timestamps_manager_v1 *manager, struct wlr_seat *seat,
	uint64_t tv_sec, uint32_t tv_nsec);

void wlr_input_timestamps_manager_v1_send_pointer_timestamp(
	struct wlr_input_timestamps_manager_v1 *manager, struct wlr_seat *seat,
	uint64_t tv_sec, uint32_t tv_nsec);

void wlr_input_timestamps_manager_v1_send_touch_timestamp(
	struct wlr_input_timestamps_manager_v1 *manager, struct wlr_seat *seat,
	uint64_t tv_sec, uint32_t tv_nsec);

#endif
