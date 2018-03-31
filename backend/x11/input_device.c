#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/util/log.h>
#include <xcb/xcb.h>
#ifdef __linux__
#include <linux/input-event-codes.h>
#elif __FreeBSD__
#include <dev/evdev/input-event-codes.h>
#endif
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

bool x11_handle_input_event(struct wlr_x11_backend *x11,
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
		return true;
	}
	case XCB_BUTTON_PRESS: {
		xcb_button_press_event_t *ev = (xcb_button_press_event_t *)event;

		if (ev->detail == XCB_BUTTON_INDEX_4 ||
				ev->detail == XCB_BUTTON_INDEX_5) {
			double delta = (ev->detail == XCB_BUTTON_INDEX_4 ? -15 : 15);
			struct wlr_event_pointer_axis axis = {
				.device = &x11->pointer_dev,
				.time_msec = ev->time,
				.source = WLR_AXIS_SOURCE_WHEEL,
				.orientation = WLR_AXIS_ORIENTATION_VERTICAL,
				.delta = delta,
			};
			wlr_signal_emit_safe(&x11->pointer.events.axis, &axis);
			x11->time = ev->time;
			break;
		}
	}
	/* fallthrough */
	case XCB_BUTTON_RELEASE: {
		xcb_button_press_event_t *ev = (xcb_button_press_event_t *)event;

		if (ev->detail != XCB_BUTTON_INDEX_4 &&
				ev->detail != XCB_BUTTON_INDEX_5) {
			struct wlr_event_pointer_button button = {
				.device = &x11->pointer_dev,
				.time_msec = ev->time,
				.button = xcb_button_to_wl(ev->detail),
				.state = event->response_type == XCB_BUTTON_PRESS ?
					WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED,
			};

			wlr_signal_emit_safe(&x11->pointer.events.button, &button);
		}
		x11->time = ev->time;
		return true;
	}
	case XCB_MOTION_NOTIFY: {
		xcb_motion_notify_event_t *ev = (xcb_motion_notify_event_t *)event;

		struct wlr_x11_output *output =
			x11_output_from_window_id(x11, ev->event);
		if (output == NULL) {
			return false;
		}
		struct wlr_output *wlr_output = &output->wlr_output;

		struct wlr_box box = { .x = ev->event_x, .y = ev->event_y };
		wlr_box_transform(&box, wlr_output->transform, wlr_output->width,
			wlr_output->height, &box);
		box.x /= wlr_output->scale;
		box.y /= wlr_output->scale;

		struct wlr_box layout_box;
		x11_output_layout_get_box(x11, &layout_box);

		double ox = wlr_output->lx / (double)layout_box.width;
		double oy = wlr_output->ly / (double)layout_box.height;

		struct wlr_event_pointer_motion_absolute wlr_event = {
			.device = &x11->pointer_dev,
			.time_msec = ev->time,
			.x = box.x / (double)layout_box.width + ox,
			.y = box.y / (double)layout_box.height + oy,
		};

		wlr_signal_emit_safe(&x11->pointer.events.motion_absolute, &wlr_event);

		x11->time = ev->time;
		return true;
	}
	default:
#ifdef WLR_HAS_XCB_XKB
		if (x11->xkb_supported && event->response_type == x11->xkb_base_event) {
			xcb_xkb_state_notify_event_t *ev =
				(xcb_xkb_state_notify_event_t *)event;
			wlr_keyboard_notify_modifiers(&x11->keyboard, ev->baseMods,
				ev->latchedMods, ev->lockedMods, ev->lockedGroup);
			return true;
		}
#endif
		break;
	}

	return false;
}

const struct wlr_input_device_impl input_device_impl = { 0 };

bool wlr_input_device_is_x11(struct wlr_input_device *wlr_dev) {
	return wlr_dev->impl == &input_device_impl;
}
