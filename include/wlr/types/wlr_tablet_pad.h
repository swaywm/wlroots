#ifndef _WLR_TYPES_TABLET_PAD_H
#define _WLR_TYPES_TABLET_PAD_H
#include <wlr/types/wlr_input_device.h>
#include <wayland-server.h>
#include <stdint.h>

/*
 * NOTE: the wlr tablet pad implementation does not currently support tablets
 * with more than one mode. I don't own any such hardware so I cannot test it
 * and it is too complicated to make a meaningful implementation of blindly.
 */

struct wlr_tablet_pad_impl;
struct wlr_tablet_pad_state;

struct wlr_tablet_pad {
	struct wlr_tablet_pad_impl *impl;
	struct wlr_tablet_pad_state *state;

	struct {
		struct wl_signal button;
		struct wl_signal ring;
		struct wl_signal strip;
	} events;
};

struct wlr_event_tablet_pad_button {
	uint32_t time_sec;
	uint64_t time_usec;
	uint32_t button;
	enum wlr_button_state state;
};

enum wlr_tablet_pad_ring_source {
	WLR_TABLET_PAD_RING_SOURCE_UNKNOWN,
	WLR_TABLET_PAD_RING_SOURCE_FINGER,
};

struct wlr_event_tablet_pad_ring {
	uint32_t time_sec;
	uint64_t time_usec;
	enum wlr_tablet_pad_ring_source source;
	uint32_t ring;
	double position;
};

enum wlr_tablet_pad_strip_source {
	WLR_TABLET_PAD_STRIP_SOURCE_UNKNOWN,
	WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
};

struct wlr_event_tablet_pad_strip {
	uint32_t time_sec;
	uint64_t time_usec;
	enum wlr_tablet_pad_strip_source source;
	uint32_t strip;
	double position;
};

#endif
