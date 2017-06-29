#define _POSIX_C_SOURCE 200112L
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-protocol.h>
#include <wlr/backend.h>
#include <wlr/session.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "shared.h"

static void keyboard_led_update(struct keyboard_state *kbstate) {
	uint32_t leds = 0;
	for (uint32_t i = 0; i < WLR_LED_LAST; ++i) {
		if (xkb_state_led_index_is_active(kbstate->xkb_state, kbstate->leds[i])) {
			leds |= (1 << i);
		}
	}
	wlr_keyboard_led_update(kbstate->device->keyboard, leds);
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_keyboard_key *event = data;
	struct keyboard_state *kbstate = wl_container_of(listener, kbstate, key);
	uint32_t keycode = event->keycode + 8;
	enum wlr_key_state key_state = event->state;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kbstate->xkb_state, keycode, &syms);
	for (int i = 0; i < nsyms; ++i) {
		xkb_keysym_t sym = syms[i];
		char name[64];
		int l = xkb_keysym_get_name(sym, name, sizeof(name));
		if (l != -1 && l != sizeof(name)) {
			wlr_log(L_DEBUG, "Key event: %s %s", name,
					key_state == WLR_KEY_PRESSED ? "pressed" : "released");
		}
		if (kbstate->compositor->keyboard_key_cb) {
			kbstate->compositor->keyboard_key_cb(kbstate, sym, key_state);
		}
		if (sym == XKB_KEY_Escape) {
			wl_display_terminate(kbstate->compositor->display);
		} else if (key_state == WLR_KEY_PRESSED && sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) {
			wlr_session_change_vt(kbstate->compositor->session, sym - XKB_KEY_F1 + 1);
		}
	}
	xkb_state_update_key(kbstate->xkb_state, keycode,
		event->state == WLR_KEY_PRESSED ?  XKB_KEY_DOWN : XKB_KEY_UP);
	keyboard_led_update(kbstate);
}

static void keyboard_add(struct wlr_input_device *device, struct compositor_state *state) {
	struct keyboard_state *kbstate = calloc(sizeof(struct keyboard_state), 1);
	kbstate->device = device;
	kbstate->compositor = state;
	wl_list_init(&kbstate->key.link);
	kbstate->key.notify = keyboard_key_notify;
	wl_signal_add(&device->keyboard->events.key, &kbstate->key);
	wl_list_insert(&state->keyboards, &kbstate->link);

	struct xkb_rule_names rules;
	memset(&rules, 0, sizeof(rules));
	rules.rules = getenv("XKB_DEFAULT_RULES");
	rules.model = getenv("XKB_DEFAULT_MODEL");
	rules.layout = getenv("XKB_DEFAULT_LAYOUT");
	rules.variant = getenv("XKB_DEFAULT_VARIANT");
	rules.options = getenv("XKB_DEFAULT_OPTIONS");
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		wlr_log(L_ERROR, "Failed to create XKB context");
		exit(1);
	}
	kbstate->keymap = xkb_map_new_from_names(
			context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!kbstate->keymap) {
		wlr_log(L_ERROR, "Failed to create XKB keymap");
		exit(1);
	}
	xkb_context_unref(context);
	kbstate->xkb_state = xkb_state_new(kbstate->keymap);
	if (!kbstate->xkb_state) {
		wlr_log(L_ERROR, "Failed to create XKB state");
		exit(1);
	}
	const char *led_names[3] = {
		XKB_LED_NAME_NUM,
		XKB_LED_NAME_CAPS,
		XKB_LED_NAME_SCROLL
	};
	for (uint32_t i = 0; i < 3; ++i) {
		kbstate->leds[i] = xkb_map_led_get_index(kbstate->keymap, led_names[i]);
	}
}

static void pointer_motion_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_motion *event = data;
	struct pointer_state *pstate = wl_container_of(listener, pstate, motion);
	if (pstate->compositor->pointer_motion_cb) {
		pstate->compositor->pointer_motion_cb(pstate,
				event->delta_x, event->delta_y);
	}
}

static void pointer_motion_absolute_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_motion_absolute *event = data;
	struct pointer_state *pstate = wl_container_of(listener, pstate, motion_absolute);
	if (pstate->compositor->pointer_motion_absolute_cb) {
		pstate->compositor->pointer_motion_absolute_cb(pstate,
				event->x_mm, event->y_mm);
	}
}

static void pointer_button_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_button *event = data;
	struct pointer_state *pstate = wl_container_of(listener, pstate, button);
	if (pstate->compositor->pointer_button_cb) {
		pstate->compositor->pointer_button_cb(pstate,
				event->button, event->state);
	}
}

static void pointer_axis_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_pointer_axis *event = data;
	struct pointer_state *pstate = wl_container_of(listener, pstate, axis);
	if (pstate->compositor->pointer_axis_cb) {
		pstate->compositor->pointer_axis_cb(pstate,
				event->source, event->orientation, event->delta);
	}
}

static void pointer_add(struct wlr_input_device *device, struct compositor_state *state) {
	struct pointer_state *pstate = calloc(sizeof(struct pointer_state), 1);
	pstate->device = device;
	pstate->compositor = state;
	wl_list_init(&pstate->motion.link);
	wl_list_init(&pstate->motion_absolute.link);
	wl_list_init(&pstate->button.link);
	wl_list_init(&pstate->axis.link);
	pstate->motion.notify = pointer_motion_notify;
	pstate->motion_absolute.notify = pointer_motion_absolute_notify;
	pstate->button.notify = pointer_button_notify;
	pstate->axis.notify = pointer_axis_notify;
	wl_signal_add(&device->pointer->events.motion, &pstate->motion);
	wl_signal_add(&device->pointer->events.motion_absolute, &pstate->motion_absolute);
	wl_signal_add(&device->pointer->events.button, &pstate->button);
	wl_signal_add(&device->pointer->events.axis, &pstate->axis);
	wl_list_insert(&state->pointers, &pstate->link);
}

static void touch_down_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_down *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, down);
	if (tstate->compositor->touch_down_cb) {
		tstate->compositor->touch_down_cb(tstate, event->slot,
			event->x_mm, event->y_mm, event->width_mm, event->height_mm);
	}
}

static void touch_motion_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_motion *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, motion);
	if (tstate->compositor->touch_motion_cb) {
		tstate->compositor->touch_motion_cb(tstate, event->slot,
			event->x_mm, event->y_mm, event->width_mm, event->height_mm);
	}
}

static void touch_up_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_up *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, up);
	if (tstate->compositor->touch_up_cb) {
		tstate->compositor->touch_up_cb(tstate, event->slot);
	}
}

static void touch_cancel_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_cancel *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, cancel);
	if (tstate->compositor->touch_cancel_cb) {
		tstate->compositor->touch_cancel_cb(tstate, event->slot);
	}
}

static void touch_add(struct wlr_input_device *device, struct compositor_state *state) {
	struct touch_state *tstate = calloc(sizeof(struct touch_state), 1);
	tstate->device = device;
	tstate->compositor = state;
	wl_list_init(&tstate->down.link);
	wl_list_init(&tstate->motion.link);
	wl_list_init(&tstate->up.link);
	wl_list_init(&tstate->cancel.link);
	tstate->down.notify = touch_down_notify;
	tstate->motion.notify = touch_motion_notify;
	tstate->up.notify = touch_up_notify;
	tstate->cancel.notify = touch_cancel_notify;
	wl_signal_add(&device->touch->events.down, &tstate->down);
	wl_signal_add(&device->touch->events.motion, &tstate->motion);
	wl_signal_add(&device->touch->events.up, &tstate->up);
	wl_signal_add(&device->touch->events.cancel, &tstate->cancel);
	wl_list_insert(&state->touch, &tstate->link);
}

static void tablet_tool_axis_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_tablet_tool_axis *event = data;
	struct tablet_tool_state *tstate = wl_container_of(listener, tstate, axis);
	if (tstate->compositor->tool_axis_cb) {
		tstate->compositor->tool_axis_cb(tstate, event);
	}
}

static void tablet_tool_proximity_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_tablet_tool_proximity *event = data;
	struct tablet_tool_state *tstate = wl_container_of(listener, tstate, proximity);
	if (tstate->compositor->tool_proximity_cb) {
		tstate->compositor->tool_proximity_cb(tstate, event->state);
	}
}

static void tablet_tool_button_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_tablet_tool_button *event = data;
	struct tablet_tool_state *tstate = wl_container_of(listener, tstate, button);
	if (tstate->compositor->tool_button_cb) {
		tstate->compositor->tool_button_cb(tstate, event->button, event->state);
	}
}

static void tablet_tool_add(struct wlr_input_device *device,
		struct compositor_state *state) {
	struct tablet_tool_state *tstate = calloc(sizeof(struct tablet_tool_state), 1);
	tstate->device = device;
	tstate->compositor = state;
	wl_list_init(&tstate->axis.link);
	wl_list_init(&tstate->proximity.link);
	wl_list_init(&tstate->tip.link);
	wl_list_init(&tstate->button.link);
	tstate->axis.notify = tablet_tool_axis_notify;
	tstate->proximity.notify = tablet_tool_proximity_notify;
	//tstate->tip.notify = tablet_tool_tip_notify;
	tstate->button.notify = tablet_tool_button_notify;
	wl_signal_add(&device->tablet_tool->events.axis, &tstate->axis);
	wl_signal_add(&device->tablet_tool->events.proximity, &tstate->proximity);
	//wl_signal_add(&device->tablet_tool->events.tip, &tstate->tip);
	wl_signal_add(&device->tablet_tool->events.button, &tstate->button);
	wl_list_insert(&state->tablet_tools, &tstate->link);
}

static void tablet_pad_button_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_tablet_pad_button *event = data;
	struct tablet_pad_state *pstate = wl_container_of(listener, pstate, button);
	if (pstate->compositor->pad_button_cb) {
		pstate->compositor->pad_button_cb(pstate, event->button, event->state);
	}
}

static void tablet_pad_add(struct wlr_input_device *device,
		struct compositor_state *state) {
	struct tablet_pad_state *pstate = calloc(sizeof(struct tablet_pad_state), 1);
	pstate->device = device;
	pstate->compositor = state;
	wl_list_init(&pstate->button.link);
	pstate->button.notify = tablet_pad_button_notify;
	wl_signal_add(&device->tablet_pad->events.button, &pstate->button);
	wl_list_insert(&state->tablet_pads, &pstate->link);
}

static void input_add_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct compositor_state *state = wl_container_of(listener, state, input_add);
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		keyboard_add(device, state);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		pointer_add(device, state);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		touch_add(device, state);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		tablet_tool_add(device, state);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		tablet_pad_add(device, state);
		break;
	default:
		break;
	}
}

static void keyboard_remove(struct wlr_input_device *device, struct compositor_state *state) {
	struct keyboard_state *kbstate = NULL, *_kbstate;
	wl_list_for_each(_kbstate, &state->keyboards, link) {
		if (_kbstate->device == device) {
			kbstate = _kbstate;
			break;
		}
	}
	if (!kbstate) {
		return;
	}
	wl_list_remove(&kbstate->link);
	wl_list_remove(&kbstate->key.link);
}

static void pointer_remove(struct wlr_input_device *device, struct compositor_state *state) {
	struct pointer_state *pstate = NULL, *_pstate;
	wl_list_for_each(_pstate, &state->pointers, link) {
		if (_pstate->device == device) {
			pstate = _pstate;
			break;
		}
	}
	if (!pstate) {
		return;
	}
	wl_list_remove(&pstate->link);
	wl_list_remove(&pstate->motion.link);
	wl_list_remove(&pstate->motion_absolute.link);
	wl_list_remove(&pstate->button.link);
	wl_list_remove(&pstate->axis.link);
}

static void touch_remove(struct wlr_input_device *device, struct compositor_state *state) {
	struct touch_state *tstate = NULL, *_tstate;
	wl_list_for_each(_tstate, &state->touch, link) {
		if (_tstate->device == device) {
			tstate = _tstate;
			break;
		}
	}
	if (!tstate) {
		return;
	}
	wl_list_remove(&tstate->link);
	wl_list_remove(&tstate->down.link);
	wl_list_remove(&tstate->motion.link);
	wl_list_remove(&tstate->up.link);
	wl_list_remove(&tstate->cancel.link);
}

static void tablet_tool_remove(struct wlr_input_device *device, struct compositor_state *state) {
	struct tablet_tool_state *tstate = NULL, *_tstate;
	wl_list_for_each(_tstate, &state->tablet_tools, link) {
		if (_tstate->device == device) {
			tstate = _tstate;
			break;
		}
	}
	if (!tstate) {
		return;
	}
	wl_list_remove(&tstate->link);
	wl_list_remove(&tstate->axis.link);
	wl_list_remove(&tstate->proximity.link);
	//wl_list_remove(&tstate->tip.link);
	wl_list_remove(&tstate->button.link);
}

static void input_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct compositor_state *state = wl_container_of(listener, state, input_add);
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		keyboard_remove(device, state);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		pointer_remove(device, state);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		touch_remove(device, state);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		tablet_tool_remove(device, state);
		break;
	default:
		break;
	}
}

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct output_state *output = wl_container_of(listener, output, frame);
	struct compositor_state *compositor = output->compositor;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (compositor->output_frame_cb) {
		compositor->output_frame_cb(output, &now);
	}

	output->last_frame = now;
	compositor->last_frame = now;
}

static void output_add_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct compositor_state *state = wl_container_of(listener, state, output_add);
	wlr_log(L_DEBUG, "Output '%s' added", output->name);
	wlr_log(L_DEBUG, "%s %s %"PRId32"mm x %"PRId32"mm", output->make, output->model,
		output->phys_width, output->phys_height);
	if (output->modes->length > 0) {
		wlr_output_set_mode(output, output->modes->items[0]);
	}
	struct output_state *ostate = calloc(1, sizeof(struct output_state));
	clock_gettime(CLOCK_MONOTONIC, &ostate->last_frame);
	ostate->output = output;
	ostate->compositor = state;
	ostate->frame.notify = output_frame_notify;
	wl_list_init(&ostate->frame.link);
	wl_signal_add(&output->events.frame, &ostate->frame);
	wl_list_insert(&state->outputs, &ostate->link);
	if (state->output_add_cb) {
		state->output_add_cb(ostate);
	}
}

static void output_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct compositor_state *state = wl_container_of(listener, state, output_remove);
	struct output_state *ostate = NULL, *_ostate;
	wl_list_for_each(_ostate, &state->outputs, link) {
		if (_ostate->output == output) {
			ostate = _ostate;
			break;
		}
	}
	if (!ostate) {
		return; // We are unfamiliar with this output
	}
	if (state->output_remove_cb) {
		state->output_remove_cb(ostate);
	}
	wl_list_remove(&ostate->link);
	wl_list_remove(&ostate->frame.link);
}

void compositor_init(struct compositor_state *state) {
	state->display = wl_display_create();
	state->event_loop = wl_display_get_event_loop(state->display);
	state->session = wlr_session_start(state->display);
	if (!state->session
			|| !state->display
			|| !state->event_loop) {
		exit(1);
	}

	wl_list_init(&state->keyboards);
	wl_list_init(&state->pointers);
	wl_list_init(&state->touch);
	wl_list_init(&state->tablet_tools);
	wl_list_init(&state->tablet_pads);
	wl_list_init(&state->input_add.link);
	state->input_add.notify = input_add_notify;
	wl_list_init(&state->input_remove.link);
	state->input_remove.notify = input_remove_notify;

	wl_list_init(&state->outputs);
	wl_list_init(&state->output_add.link);
	state->output_add.notify = output_add_notify;
	wl_list_init(&state->output_remove.link);
	state->output_remove.notify = output_remove_notify;

	struct wlr_backend *wlr = wlr_backend_autocreate(
			state->display, state->session);
	if (!wlr) {
		exit(1);
	}
	wl_signal_add(&wlr->events.input_add, &state->input_add);
	wl_signal_add(&wlr->events.input_remove, &state->input_remove);
	wl_signal_add(&wlr->events.output_add, &state->output_add);
	wl_signal_add(&wlr->events.output_remove, &state->output_remove);
	state->backend = wlr;

	clock_gettime(CLOCK_MONOTONIC, &state->last_frame);

	const char *socket = wl_display_add_socket_auto(state->display);
	wlr_log(L_INFO, "Running compositor on wayland display '%s'", socket);
	setenv("_WAYLAND_DISPLAY", socket, true);
	if (!wlr_backend_init(state->backend)) {
		wlr_log(L_ERROR, "Failed to initialize backend");
		exit(1);
	}
}

void compositor_run(struct compositor_state *state) {
	wl_display_run(state->display);
	wlr_backend_destroy(state->backend);
	wlr_session_finish(state->session);
	wl_display_destroy(state->display);
}
