#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-protocol.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "shared.h"

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_keyboard_key *event = data;
	struct keyboard_state *kbstate = wl_container_of(listener, kbstate, key);
	uint32_t keycode = event->keycode + 8;
	enum wlr_key_state key_state = event->state;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kbstate->device->keyboard->xkb_state,
			keycode, &syms);

	for (int i = 0; i < nsyms; ++i) {
		xkb_keysym_t sym = syms[i];
		char name[64];
		int l = xkb_keysym_get_name(sym, name, sizeof(name));
		if (l != -1 && l != sizeof(name)) {
			wlr_log(L_DEBUG, "Key event: %s %s", name,
					key_state == WLR_KEY_PRESSED ? "pressed" : "released");
		}
		if (kbstate->compositor->keyboard_key_cb) {
			kbstate->compositor->keyboard_key_cb(kbstate, event->keycode, sym,
				key_state, event->time_msec * 1000);
		}
		if (sym == XKB_KEY_Escape) {
			wl_display_terminate(kbstate->compositor->display);
		} else if (key_state == WLR_KEY_PRESSED &&
				sym >= XKB_KEY_XF86Switch_VT_1 &&
				sym <= XKB_KEY_XF86Switch_VT_12) {
			if (wlr_backend_is_multi(kbstate->compositor->backend)) {
				struct wlr_session *session =
					wlr_multi_get_session(kbstate->compositor->backend);
				if (session) {
					wlr_session_change_vt(session, sym - XKB_KEY_XF86Switch_VT_1 + 1);
				}
			}
		}
	}
}

static void keyboard_destroy_notify(struct wl_listener *listener, void *data) {
	struct keyboard_state *kbstate = wl_container_of(listener, kbstate, destroy);
	struct compositor_state *state = kbstate->compositor;
	if (state->input_remove_cb) {
		state->input_remove_cb(state, kbstate->device);
	}
	wl_list_remove(&kbstate->link);
	wl_list_remove(&kbstate->destroy.link);
	wl_list_remove(&kbstate->key.link);
	free(kbstate);
}

static void keyboard_add(struct wlr_input_device *device, struct compositor_state *state) {
	struct keyboard_state *kbstate = calloc(sizeof(struct keyboard_state), 1);
	kbstate->device = device;
	kbstate->compositor = state;
	kbstate->destroy.notify = keyboard_destroy_notify;
	wl_signal_add(&device->events.destroy, &kbstate->destroy);
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
	wlr_keyboard_set_keymap(device->keyboard, xkb_map_new_from_names(context,
				&rules, XKB_KEYMAP_COMPILE_NO_FLAGS));
	xkb_context_unref(context);
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
		pstate->compositor->pointer_motion_absolute_cb(
				pstate, event->x, event->y);
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

static void pointer_destroy_notify(struct wl_listener *listener, void *data) {
	struct pointer_state *pstate = wl_container_of(listener, pstate, destroy);
	struct compositor_state *state = pstate->compositor;
	if (state->input_remove_cb) {
		state->input_remove_cb(state, pstate->device);
	}
	wl_list_remove(&pstate->link);
	wl_list_remove(&pstate->destroy.link);
	wl_list_remove(&pstate->motion.link);
	wl_list_remove(&pstate->motion_absolute.link);
	wl_list_remove(&pstate->button.link);
	wl_list_remove(&pstate->axis.link);
	free(pstate);
}

static void pointer_add(struct wlr_input_device *device, struct compositor_state *state) {
	struct pointer_state *pstate = calloc(sizeof(struct pointer_state), 1);
	pstate->device = device;
	pstate->compositor = state;
	pstate->destroy.notify = pointer_destroy_notify;
	wl_signal_add(&device->events.destroy, &pstate->destroy);
	pstate->motion.notify = pointer_motion_notify;
	wl_signal_add(&device->pointer->events.motion, &pstate->motion);
	pstate->motion_absolute.notify = pointer_motion_absolute_notify;
	wl_signal_add(&device->pointer->events.motion_absolute, &pstate->motion_absolute);
	pstate->button.notify = pointer_button_notify;
	wl_signal_add(&device->pointer->events.button, &pstate->button);
	pstate->axis.notify = pointer_axis_notify;
	wl_signal_add(&device->pointer->events.axis, &pstate->axis);
	wl_list_insert(&state->pointers, &pstate->link);
}

static void touch_down_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_down *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, down);
	if (tstate->compositor->touch_down_cb) {
		tstate->compositor->touch_down_cb(tstate,
				event->touch_id, event->x, event->y);
	}
}

static void touch_motion_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_motion *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, motion);
	if (tstate->compositor->touch_motion_cb) {
		tstate->compositor->touch_motion_cb(tstate,
				event->touch_id, event->x, event->y);
	}
}

static void touch_up_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_up *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, up);
	if (tstate->compositor->touch_up_cb) {
		tstate->compositor->touch_up_cb(tstate, event->touch_id);
	}
}

static void touch_cancel_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_touch_cancel *event = data;
	struct touch_state *tstate = wl_container_of(listener, tstate, cancel);
	if (tstate->compositor->touch_cancel_cb) {
		tstate->compositor->touch_cancel_cb(tstate, event->touch_id);
	}
}

static void touch_destroy_notify(struct wl_listener *listener, void *data) {
	struct touch_state *tstate = wl_container_of(listener, tstate, destroy);
	struct compositor_state *state = tstate->compositor;
	if (state->input_remove_cb) {
		state->input_remove_cb(state, tstate->device);
	}
	wl_list_remove(&tstate->link);
	wl_list_remove(&tstate->destroy.link);
	wl_list_remove(&tstate->down.link);
	wl_list_remove(&tstate->motion.link);
	wl_list_remove(&tstate->up.link);
	wl_list_remove(&tstate->cancel.link);
	free(tstate);
}

static void touch_add(struct wlr_input_device *device, struct compositor_state *state) {
	struct touch_state *tstate = calloc(sizeof(struct touch_state), 1);
	tstate->device = device;
	tstate->compositor = state;
	tstate->destroy.notify = touch_destroy_notify;
	wl_signal_add(&device->events.destroy, &tstate->destroy);
	tstate->down.notify = touch_down_notify;
	wl_signal_add(&device->touch->events.down, &tstate->down);
	tstate->motion.notify = touch_motion_notify;
	wl_signal_add(&device->touch->events.motion, &tstate->motion);
	tstate->up.notify = touch_up_notify;
	wl_signal_add(&device->touch->events.up, &tstate->up);
	tstate->cancel.notify = touch_cancel_notify;
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

static void tablet_tool_destroy_notify(struct wl_listener *listener, void *data) {
	struct tablet_tool_state *tstate = wl_container_of(listener, tstate, destroy);
	struct compositor_state *state = tstate->compositor;
	if (state->input_remove_cb) {
		state->input_remove_cb(state, tstate->device);
	}
	wl_list_remove(&tstate->link);
	wl_list_remove(&tstate->destroy.link);
	wl_list_remove(&tstate->axis.link);
	wl_list_remove(&tstate->proximity.link);
	//wl_list_remove(&tstate->tip.link);
	wl_list_remove(&tstate->button.link);
	free(tstate);
}

static void tablet_tool_add(struct wlr_input_device *device,
		struct compositor_state *state) {
	struct tablet_tool_state *tstate = calloc(sizeof(struct tablet_tool_state), 1);
	tstate->device = device;
	tstate->compositor = state;
	tstate->destroy.notify = tablet_tool_destroy_notify;
	wl_signal_add(&device->events.destroy, &tstate->destroy);
	tstate->axis.notify = tablet_tool_axis_notify;
	wl_signal_add(&device->tablet_tool->events.axis, &tstate->axis);
	tstate->proximity.notify = tablet_tool_proximity_notify;
	wl_signal_add(&device->tablet_tool->events.proximity, &tstate->proximity);
	//tstate->tip.notify = tablet_tool_tip_notify;
	//wl_signal_add(&device->tablet_tool->events.tip, &tstate->tip);
	tstate->button.notify = tablet_tool_button_notify;
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

static void tablet_pad_ring_notify(struct wl_listener *listener, void *data) {
	struct wlr_event_tablet_pad_ring *event = data;
	struct tablet_pad_state *pstate = wl_container_of(listener, pstate, ring);
	if (pstate->compositor->pad_ring_cb) {
		pstate->compositor->pad_ring_cb(pstate, event->ring, event->position);
	}
}

static void tablet_pad_destroy_notify(struct wl_listener *listener, void *data) {
	struct tablet_pad_state *pstate = wl_container_of(listener, pstate, destroy);
	struct compositor_state *state = pstate->compositor;
	if (state->input_remove_cb) {
		state->input_remove_cb(state, pstate->device);
	}
	wl_list_remove(&pstate->link);
	wl_list_remove(&pstate->destroy.link);
	wl_list_remove(&pstate->button.link);
	free(pstate);
}

static void tablet_pad_add(struct wlr_input_device *device,
		struct compositor_state *state) {
	struct tablet_pad_state *pstate = calloc(sizeof(struct tablet_pad_state), 1);
	pstate->device = device;
	pstate->compositor = state;
	pstate->destroy.notify = tablet_pad_destroy_notify;
	wl_signal_add(&device->events.destroy, &pstate->destroy);
	pstate->button.notify = tablet_pad_button_notify;
	wl_signal_add(&device->tablet_pad->events.button, &pstate->button);
	pstate->ring.notify = tablet_pad_ring_notify;
	wl_signal_add(&device->tablet_pad->events.ring, &pstate->ring);
	wl_list_insert(&state->tablet_pads, &pstate->link);
}

static void new_input_notify(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;
	struct compositor_state *state = wl_container_of(listener, state, new_input);
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

	if (state->input_add_cb) {
		state->input_add_cb(state, device);
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

static void output_resolution_notify(struct wl_listener *listener, void *data) {
	struct output_state *output = wl_container_of(listener, output, resolution);
	struct compositor_state *compositor = output->compositor;

	if (compositor->output_resolution_cb) {
		compositor->output_resolution_cb(compositor, output);
	}
}

static void output_destroy_notify(struct wl_listener *listener, void *data) {
	struct output_state *ostate = wl_container_of(listener, ostate, destroy);
	struct compositor_state *state = ostate->compositor;
	if (state->output_remove_cb) {
		state->output_remove_cb(ostate);
	}
	wl_list_remove(&ostate->link);
	wl_list_remove(&ostate->frame.link);
	wl_list_remove(&ostate->resolution.link);
	free(ostate);
}

static void new_output_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *output = data;
	struct compositor_state *state = wl_container_of(listener, state, new_output);
	wlr_log(L_DEBUG, "Output '%s' added", output->name);
	wlr_log(L_DEBUG, "%s %s %"PRId32"mm x %"PRId32"mm", output->make, output->model,
		output->phys_width, output->phys_height);
	if (wl_list_length(&output->modes) > 0) {
		struct wlr_output_mode *mode;
		mode = wl_container_of((&output->modes)->prev, mode, link);
		wlr_output_set_mode(output, mode);
	}
	struct output_state *ostate = calloc(1, sizeof(struct output_state));
	clock_gettime(CLOCK_MONOTONIC, &ostate->last_frame);
	ostate->output = output;
	ostate->compositor = state;
	ostate->destroy.notify = output_destroy_notify;
	wl_signal_add(&output->events.destroy, &ostate->destroy);
	ostate->frame.notify = output_frame_notify;
	wl_signal_add(&output->events.frame, &ostate->frame);
	ostate->resolution.notify = output_resolution_notify;
	wl_signal_add(&output->events.mode, &ostate->resolution);
	wl_list_insert(&state->outputs, &ostate->link);
	if (state->output_add_cb) {
		state->output_add_cb(ostate);
	}
}

void compositor_init(struct compositor_state *state) {
	state->display = wl_display_create();
	state->event_loop = wl_display_get_event_loop(state->display);

	wl_list_init(&state->keyboards);
	wl_list_init(&state->pointers);
	wl_list_init(&state->touch);
	wl_list_init(&state->tablet_tools);
	wl_list_init(&state->tablet_pads);
	wl_list_init(&state->outputs);

	struct wlr_backend *wlr = wlr_backend_autocreate(state->display);
	if (!wlr) {
		exit(1);
	}
	state->backend = wlr;

	wl_signal_add(&wlr->events.new_input, &state->new_input);
	state->new_input.notify = new_input_notify;
	wl_signal_add(&wlr->events.new_output, &state->new_output);
	state->new_output.notify = new_output_notify;

	clock_gettime(CLOCK_MONOTONIC, &state->last_frame);

	const char *socket = wl_display_add_socket_auto(state->display);
	if (!socket) {
		wlr_log_errno(L_ERROR, "Unable to open wayland socket");
		wlr_backend_destroy(wlr);
		exit(1);
	}

	wlr_log(L_INFO, "Running compositor on wayland display '%s'", socket);
	setenv("_WAYLAND_DISPLAY", socket, true);
}

void compositor_fini(struct compositor_state *state) {
	wl_display_destroy(state->display);
}
