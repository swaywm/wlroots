#ifndef WLR_TYPES_WLR_TABLET_PAD_H
#define WLR_TYPES_WLR_TABLET_PAD_H

#include <stdint.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>

/*
 * NOTE: the wlr tablet pad implementation does not currently support tablets
 * with more than one mode. I don't own any such hardware so I cannot test it
 * and it is too complicated to make a meaningful implementation of blindly.
 */

struct wlr_tablet_pad_impl;

struct wlr_tablet_pad {
	struct wlr_tablet_pad_impl *impl;

	struct {
		struct wl_signal button;
		struct wl_signal ring;
		struct wl_signal strip;
	} events;

	size_t button_count;
	size_t ring_count;
	size_t strip_count;

	struct wl_list groups; // wlr_tablet_pad_group::link
	struct wl_list paths; // wlr_tablet_path::link

	void *data;
};

struct wlr_tablet_pad_group {
	struct wl_list link;

	size_t button_count;
	unsigned int *buttons;

	size_t strip_count;
	unsigned int *strips;

	size_t ring_count;
	unsigned int *rings;

	unsigned int mode_count;
};

struct wlr_event_tablet_pad_button {
	uint32_t time_msec;
	uint32_t button;
	enum wlr_button_state state;
	unsigned int mode;
};

enum wlr_tablet_pad_ring_source {
	WLR_TABLET_PAD_RING_SOURCE_UNKNOWN,
	WLR_TABLET_PAD_RING_SOURCE_FINGER,
};

struct wlr_event_tablet_pad_ring {
	uint32_t time_msec;
	enum wlr_tablet_pad_ring_source source;
	uint32_t ring;
	double position;
	unsigned int mode;
};

enum wlr_tablet_pad_strip_source {
	WLR_TABLET_PAD_STRIP_SOURCE_UNKNOWN,
	WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
};

struct wlr_event_tablet_pad_strip {
	uint32_t time_msec;
	enum wlr_tablet_pad_strip_source source;
	uint32_t strip;
	double position;
	unsigned int mode;
};

#endif
