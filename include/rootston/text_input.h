#ifndef ROOTSTON_TEXT_INPUT_H
#define ROOTSTON_TEXT_INPUT_H

#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_surface.h>
#include "rootston/seat.h"

/**
 * The relay structure manages the relationship between text-input and
 * input_method interfaces on a given seat. Multiple text-input interfaces may
 * be bound to a relay, but at most one will be focused (reveiving events) at
 * a time. At most one input-method interface may be bound to the seat. The
 * relay manages life cycle of both sides. When both sides are present and
 * focused, the relay passes messages between them.
 *
 * Text input focus is a subset of keyboard focus - if the text-input is
 * in the focused state, wl_keyboard sent an enter as well. However, having
 * wl_keyboard focused doesn't mean that text-input will be focused.
 */
struct roots_input_method_relay {
	struct roots_seat *seat;

	struct wl_list text_inputs; // roots_text_input::link
	struct wlr_input_method_v2 *input_method; // doesn't have to be present

	struct wl_listener text_input_new;
	struct wl_listener text_input_enable;
	struct wl_listener text_input_commit;
	struct wl_listener text_input_disable;
	struct wl_listener text_input_destroy;

	struct wl_listener input_method_new;
	struct wl_listener input_method_commit;
	struct wl_listener input_method_destroy;
};

struct roots_text_input {
	struct roots_input_method_relay *relay;

	struct wlr_text_input_v3 *input;
	// The surface getting seat's focus. Stored for when text-input cannot
	// be sent an enter event immediately after getting focus, e.g. when
	// there's no input method available. Cleared once text-input is entered.
	struct wlr_surface *pending_focused_surface;

	struct wl_list link;

	struct wl_listener pending_focused_surface_destroy;
};

void roots_input_method_relay_init(struct roots_seat *seat,
	struct roots_input_method_relay *relay);

// Updates currently focused surface. Surface must belong to the same seat.
void roots_input_method_relay_set_focus(struct roots_input_method_relay *relay,
	struct wlr_surface *surface);

struct roots_text_input *roots_text_input_create(
	struct roots_input_method_relay *relay,
	struct wlr_text_input_v3 *text_input);

#endif
