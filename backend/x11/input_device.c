#include <stdlib.h>

#include <wlr/config.h>

#include <linux/input-event-codes.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xinput.h>

#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/util/log.h>

#include "backend/x11.h"
#include "util/signal.h"

static void send_key_event(struct wlr_x11_backend *x11, uint32_t key,
		enum wlr_key_state st, xcb_timestamp_t time) {
	struct wlr_event_keyboard_key ev = {
		.time_msec = time,
		.keycode = key,
		.state = st,
		.update_state = true,
	};
	wlr_keyboard_notify_key(&x11->keyboard, &ev);
}

static void send_button_event(struct wlr_x11_output *output, uint32_t key,
		enum wlr_button_state st, xcb_timestamp_t time) {
	struct wlr_event_pointer_button ev = {
		.device = &output->pointer_dev,
		.time_msec = time,
		.button = key,
		.state = st,
	};
	wlr_signal_emit_safe(&output->pointer.events.button, &ev);
}

static void send_axis_event(struct wlr_x11_output *output, int32_t delta,
		xcb_timestamp_t time) {
	struct wlr_event_pointer_axis ev = {
		.device = &output->pointer_dev,
		.time_msec = time,
		.source = WLR_AXIS_SOURCE_WHEEL,
		.orientation = WLR_AXIS_ORIENTATION_VERTICAL,
		// 15 is a typical value libinput sends for one scroll
		.delta = delta * 15,
		.delta_discrete = delta,
	};
	wlr_signal_emit_safe(&output->pointer.events.axis, &ev);
	wlr_signal_emit_safe(&output->pointer.events.frame, &output->pointer);
}

static void send_pointer_position_event(struct wlr_x11_output *output,
		int16_t x, int16_t y, xcb_timestamp_t time) {
	struct wlr_event_pointer_motion_absolute ev = {
		.device = &output->pointer_dev,
		.time_msec = time,
		.x = (double)x / output->wlr_output.width,
		.y = (double)y / output->wlr_output.height,
	};
	wlr_signal_emit_safe(&output->pointer.events.motion_absolute, &ev);
	wlr_signal_emit_safe(&output->pointer.events.frame, &output->pointer);
}

void handle_x11_xinput_event(struct wlr_x11_backend *x11,
		xcb_ge_generic_event_t *event) {
	struct wlr_x11_output *output;

	switch (event->event_type) {
	case XCB_INPUT_KEY_PRESS: {
		xcb_input_key_press_event_t *ev =
			(xcb_input_key_press_event_t *)event;

		wlr_keyboard_notify_modifiers(&x11->keyboard, ev->mods.base,
			ev->mods.latched, ev->mods.locked, ev->mods.effective);
		send_key_event(x11, ev->detail - 8, WLR_KEY_PRESSED, ev->time);
		x11->time = ev->time;
		break;
	}
	case XCB_INPUT_KEY_RELEASE: {
		xcb_input_key_release_event_t *ev =
			(xcb_input_key_release_event_t *)event;

		wlr_keyboard_notify_modifiers(&x11->keyboard, ev->mods.base,
			ev->mods.latched, ev->mods.locked, ev->mods.effective);
		send_key_event(x11, ev->detail - 8, WLR_KEY_RELEASED, ev->time);
		x11->time = ev->time;
		break;
	}
	case XCB_INPUT_BUTTON_PRESS: {
		xcb_input_button_press_event_t *ev =
			(xcb_input_button_press_event_t *)event;

		output = get_x11_output_from_window_id(x11, ev->event);
		if (!output) {
			return;
		}

		switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			send_button_event(output, BTN_LEFT, WLR_BUTTON_PRESSED,
				ev->time);
			break;
		case XCB_BUTTON_INDEX_2:
			send_button_event(output, BTN_MIDDLE, WLR_BUTTON_PRESSED,
				ev->time);
			break;
		case XCB_BUTTON_INDEX_3:
			send_button_event(output, BTN_RIGHT, WLR_BUTTON_PRESSED,
				ev->time);
			break;
		case XCB_BUTTON_INDEX_4:
			send_axis_event(output, -1, ev->time);
			break;
		case XCB_BUTTON_INDEX_5:
			send_axis_event(output, 1, ev->time);
			break;
		}

		x11->time = ev->time;
		break;
	}
	case XCB_INPUT_BUTTON_RELEASE: {
		xcb_input_button_release_event_t *ev =
			(xcb_input_button_release_event_t *)event;

		output = get_x11_output_from_window_id(x11, ev->event);
		if (!output) {
			return;
		}

		switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			send_button_event(output, BTN_LEFT, WLR_BUTTON_RELEASED,
				ev->time);
			break;
		case XCB_BUTTON_INDEX_2:
			send_button_event(output, BTN_MIDDLE, WLR_BUTTON_RELEASED,
				ev->time);
			break;
		case XCB_BUTTON_INDEX_3:
			send_button_event(output, BTN_RIGHT, WLR_BUTTON_RELEASED,
				ev->time);
			break;
		}

		x11->time = ev->time;
		break;
	}
	case XCB_INPUT_MOTION: {
		xcb_input_motion_event_t *ev = (xcb_input_motion_event_t *)event;

		output = get_x11_output_from_window_id(x11, ev->event);
		if (!output) {
			return;
		}

		send_pointer_position_event(output, ev->event_x >> 16,
			ev->event_y >> 16, ev->time);
		x11->time = ev->time;
		break;
	}
	case XCB_INPUT_ENTER: {
		xcb_input_enter_event_t *ev = (xcb_input_enter_event_t *)event;

		output = get_x11_output_from_window_id(x11, ev->event);
		if (!output) {
			return;
		}

		if (!output->cursor_hidden) {
			xcb_xfixes_hide_cursor(x11->xcb, output->win);
			xcb_flush(x11->xcb);
			output->cursor_hidden = true;
		}
		break;
	}
	case XCB_INPUT_LEAVE: {
		xcb_input_leave_event_t *ev = (xcb_input_leave_event_t *)event;

		output = get_x11_output_from_window_id(x11, ev->event);
		if (!output) {
			return;
		}

		if (output->cursor_hidden) {
			xcb_xfixes_show_cursor(x11->xcb, output->win);
			xcb_flush(x11->xcb);
			output->cursor_hidden = false;
		}
		break;
	}
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

	send_pointer_position_event(output, reply->win_x, reply->win_y, time);

	free(reply);
}

bool wlr_input_device_is_x11(struct wlr_input_device *wlr_dev) {
	return wlr_dev->impl == &input_device_impl;
}
