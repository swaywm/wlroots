#include <stdlib.h>

#include <wlr/config.h>

#ifdef __linux__
#include <linux/input-event-codes.h>
#elif __FreeBSD__
#include <dev/evdev/input-event-codes.h>
#endif

#include <xcb/xcb.h>
#if WLR_HAS_XCB_XKB
#include <xcb/xkb.h>
#endif

#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/util/log.h>

#include "backend/x11.h"
#include "util/signal.h"

static uint32_t xcb_button_to_wl(uint32_t button) {
	switch (button) {
	case XCB_BUTTON_INDEX_1: return BTN_LEFT;
	case XCB_BUTTON_INDEX_2: return BTN_MIDDLE;
	case XCB_BUTTON_INDEX_3: return BTN_RIGHT;
	// XXX: I'm not sure the scroll-wheel direction is right
	case XCB_BUTTON_INDEX_4: return BTN_GEAR_UP;
	case XCB_BUTTON_INDEX_5: return BTN_GEAR_DOWN;
	default: return 0;
	}
}

static void x11_handle_pointer_position(struct wlr_x11_output *output,
		int16_t x, int16_t y, xcb_timestamp_t time) {
	struct wlr_x11_backend *x11 = output->x11;
	struct wlr_output *wlr_output = &output->wlr_output;
	struct wlr_event_pointer_motion_absolute event = {
		.device = &output->pointer_dev,
		.time_msec = time,
		.x = (double)x / wlr_output->width,
		.y = (double)y / wlr_output->height,
	};
	wlr_signal_emit_safe(&output->pointer.events.motion_absolute, &event);

	x11->time = time;
}

void handle_x11_input_event(struct wlr_x11_backend *x11,
		xcb_generic_event_t *event) {
	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
	case XCB_KEY_PRESS:
	case XCB_KEY_RELEASE: {
		xcb_key_press_event_t *ev = (xcb_key_press_event_t *)event;
		struct wlr_event_keyboard_key key = {
			.time_msec = ev->time,
			.keycode = ev->detail - 8,
			.state = event->response_type == XCB_KEY_PRESS ?
				WLR_KEY_PRESSED : WLR_KEY_RELEASED,
			.update_state = true,
		};

		// TODO use xcb-xkb for more precise modifiers state?
		wlr_keyboard_notify_key(&x11->keyboard, &key);
		x11->time = ev->time;
		return;
	}
	case XCB_BUTTON_PRESS: {
		xcb_button_press_event_t *ev = (xcb_button_press_event_t *)event;

		struct wlr_x11_output *output =
			get_x11_output_from_window_id(x11, ev->event);
		if (output == NULL) {
			break;
		}

		if (ev->detail == XCB_BUTTON_INDEX_4 ||
				ev->detail == XCB_BUTTON_INDEX_5) {
			int32_t delta_discrete = ev->detail == XCB_BUTTON_INDEX_4 ? -1 : 1;
			struct wlr_event_pointer_axis axis = {
				.device = &output->pointer_dev,
				.time_msec = ev->time,
				.source = WLR_AXIS_SOURCE_WHEEL,
				.orientation = WLR_AXIS_ORIENTATION_VERTICAL,
				// 15 is a typical value libinput sends for one scroll
				.delta = delta_discrete * 15,
				.delta_discrete = delta_discrete,
			};
			wlr_signal_emit_safe(&output->pointer.events.axis, &axis);
			x11->time = ev->time;
			break;
		}
	}
	/* fallthrough */
	case XCB_BUTTON_RELEASE: {
		xcb_button_press_event_t *ev = (xcb_button_press_event_t *)event;

		struct wlr_x11_output *output =
			get_x11_output_from_window_id(x11, ev->event);
		if (output == NULL) {
			break;
		}

		if (ev->detail != XCB_BUTTON_INDEX_4 &&
				ev->detail != XCB_BUTTON_INDEX_5) {
			struct wlr_event_pointer_button button = {
				.device = &output->pointer_dev,
				.time_msec = ev->time,
				.button = xcb_button_to_wl(ev->detail),
				.state = event->response_type == XCB_BUTTON_PRESS ?
					WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED,
			};

			wlr_signal_emit_safe(&output->pointer.events.button, &button);
		}
		x11->time = ev->time;
		return;
	}
	case XCB_MOTION_NOTIFY: {
		xcb_motion_notify_event_t *ev = (xcb_motion_notify_event_t *)event;

		struct wlr_x11_output *output =
			get_x11_output_from_window_id(x11, ev->event);
		if (output != NULL) {
			x11_handle_pointer_position(output, ev->event_x, ev->event_y, ev->time);
		}
		return;
	}
	default:
#if WLR_HAS_XCB_XKB
		if (x11->xkb_supported && event->response_type == x11->xkb_base_event) {
			xcb_xkb_state_notify_event_t *ev =
				(xcb_xkb_state_notify_event_t *)event;
			wlr_keyboard_notify_modifiers(&x11->keyboard, ev->baseMods,
				ev->latchedMods, ev->lockedMods, ev->lockedGroup);
			return;
		}
#endif
		break;
	}
}

static void input_device_destroy(struct wlr_input_device *wlr_device) {
	// Don't free the input device, it's on the stack
}

const struct wlr_input_device_impl input_device_impl = {
	.destroy = input_device_destroy,
};

static void keyboard_destroy(struct wlr_keyboard *wlr_keyboard) {
	// Don't free the keyboard, it's on the stack
}

const struct wlr_keyboard_impl keyboard_impl = {
	.destroy = keyboard_destroy,
};

static void pointer_destroy(struct wlr_pointer *wlr_pointer) {
	// Don't free the pointer, it's on the stack
}

const struct wlr_pointer_impl pointer_impl = {
	.destroy = pointer_destroy,
};

void update_x11_pointer_position(struct wlr_x11_output *output,
		xcb_timestamp_t time) {
	struct wlr_x11_backend *x11 = output->x11;

	xcb_query_pointer_cookie_t cookie =
		xcb_query_pointer(x11->xcb, output->win);
	xcb_query_pointer_reply_t *reply =
		xcb_query_pointer_reply(x11->xcb, cookie, NULL);
	if (!reply) {
		return;
	}

	x11_handle_pointer_position(output, reply->win_x, reply->win_y, time);

	free(reply);
}

bool wlr_input_device_is_x11(struct wlr_input_device *wlr_dev) {
	return wlr_dev->impl == &input_device_impl;
}
