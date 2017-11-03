#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "rootston/input.h"
#include "rootston/seat.h"
#include "rootston/keyboard.h"

static ssize_t keyboard_pressed_keysym_index(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	for (size_t i = 0; i < ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP; i++) {
		if (keyboard->pressed_keysyms[i] == keysym) {
			return i;
		}
	}
	return -1;
}

static const char *exec_prefix = "exec ";

static void keyboard_binding_execute(struct roots_keyboard *keyboard,
		const char *command) {
	struct roots_server *server = keyboard->input->server;
	if (strcmp(command, "exit") == 0) {
		wl_display_terminate(server->wl_display);
	} else if (strcmp(command, "close") == 0) {
		if (server->desktop->views->length > 0) {
			struct roots_view *view =
				server->desktop->views->items[server->desktop->views->length-1];
			view_close(view);
		}
	} else if (strcmp(command, "next_window") == 0) {
		if (server->desktop->views->length > 0) {
			struct roots_view *view = server->desktop->views->items[0];
			set_view_focus(keyboard->input, server->desktop, view);
		}
	} else if (strncmp(exec_prefix, command, strlen(exec_prefix)) == 0) {
		const char *shell_cmd = command + strlen(exec_prefix);
		pid_t pid = fork();
		if (pid < 0) {
			wlr_log(L_ERROR, "cannot execute binding command: fork() failed");
			return;
		} else if (pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", shell_cmd, (void *)NULL);
		}
	} else {
		wlr_log(L_ERROR, "unknown binding command: %s", command);
	}
}

/**
 * Process a keypress from the keyboard.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
 */
static bool keyboard_keysym_press(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	ssize_t i = keyboard_pressed_keysym_index(keyboard, keysym);
	if (i < 0) {
		i = keyboard_pressed_keysym_index(keyboard, XKB_KEY_NoSymbol);
		if (i >= 0) {
			keyboard->pressed_keysyms[i] = keysym;
		}
	}

	if (keysym >= XKB_KEY_XF86Switch_VT_1 &&
			keysym <= XKB_KEY_XF86Switch_VT_12) {
		struct roots_server *server = keyboard->input->server;
		if (wlr_backend_is_multi(server->backend)) {
			struct wlr_session *session =
				wlr_multi_get_session(server->backend);
			if (session) {
				unsigned vt = keysym - XKB_KEY_XF86Switch_VT_1 + 1;
				wlr_session_change_vt(session, vt);
			}
		}
		return true;
	}

	if (keysym == XKB_KEY_Escape) {
		wlr_seat_pointer_end_grab(keyboard->seat->seat);
		wlr_seat_keyboard_end_grab(keyboard->seat->seat);
	}

	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
	struct wl_list *bindings = &keyboard->input->server->config->bindings;
	struct binding_config *bc;
	wl_list_for_each(bc, bindings, link) {
		if (modifiers ^ bc->modifiers) {
			continue;
		}

		bool ok = true;
		for (size_t i = 0; i < bc->keysyms_len; i++) {
			ssize_t j = keyboard_pressed_keysym_index(keyboard, bc->keysyms[i]);
			if (j < 0) {
				ok = false;
				break;
			}
		}

		if (ok) {
			keyboard_binding_execute(keyboard, bc->command);
			return true;
		}
	}

	return false;
}

static void keyboard_keysym_release(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
	ssize_t i = keyboard_pressed_keysym_index(keyboard, keysym);
	if (i >= 0) {
		keyboard->pressed_keysyms[i] = XKB_KEY_NoSymbol;
	}
}

void roots_keyboard_handle_key(struct roots_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int syms_len = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
		keycode, &syms);

	bool handled = false;
	for (int i = 0; i < syms_len; i++) {
		if (event->state == WLR_KEY_PRESSED) {
			bool keysym_handled = keyboard_keysym_press(keyboard, syms[i]);
			handled = handled || keysym_handled;
		} else { // WLR_KEY_RELEASED
			keyboard_keysym_release(keyboard, syms[i]);
		}
	}

	if (!handled) {
		wlr_seat_set_keyboard(keyboard->seat->seat, keyboard->device);
		wlr_seat_keyboard_notify_key(keyboard->seat->seat, event->time_msec,
			event->keycode, event->state);
	}
}

void roots_keyboard_handle_modifiers(struct roots_keyboard *r_keyboard,
		struct wlr_event_keyboard_modifiers *event) {
	struct wlr_seat *seat = r_keyboard->seat->seat;
	struct wlr_keyboard *keyboard = r_keyboard->device->keyboard;
	wlr_seat_set_keyboard(seat, r_keyboard->device);
	wlr_seat_keyboard_notify_modifiers(seat,
		keyboard->modifiers.depressed,
		keyboard->modifiers.latched,
		keyboard->modifiers.locked,
		keyboard->modifiers.group);
}

static void keyboard_config_merge(struct keyboard_config *config,
		struct keyboard_config *fallback) {
	if (fallback == NULL) {
		return;
	}
	if (config->rules == NULL) {
		config->rules = fallback->rules;
	}
	if (config->model == NULL) {
		config->model = fallback->model;
	}
	if (config->layout == NULL) {
		config->layout = fallback->layout;
	}
	if (config->variant == NULL) {
		config->variant = fallback->variant;
	}
	if (config->options == NULL) {
		config->options = fallback->options;
	}
}

struct roots_keyboard *roots_keyboard_create(struct wlr_input_device *device,
		struct roots_input *input) {
	struct roots_keyboard *keyboard = calloc(sizeof(struct roots_keyboard), 1);
	if (keyboard == NULL) {
		return NULL;
	}
	device->data = keyboard;
	keyboard->device = device;
	keyboard->input = input;

	// XXX temporary
	wl_list_insert(&input->keyboards, &keyboard->link);

	struct keyboard_config config;
	memset(&config, 0, sizeof(config));
	keyboard_config_merge(&config, config_get_keyboard(input->config, device));
	keyboard_config_merge(&config, config_get_keyboard(input->config, NULL));

	struct keyboard_config env_config = {
		.rules = getenv("XKB_DEFAULT_RULES"),
		.model = getenv("XKB_DEFAULT_MODEL"),
		.layout = getenv("XKB_DEFAULT_LAYOUT"),
		.variant = getenv("XKB_DEFAULT_VARIANT"),
		.options = getenv("XKB_DEFAULT_OPTIONS"),
	};
	keyboard_config_merge(&config, &env_config);

	struct xkb_rule_names rules;
	memset(&rules, 0, sizeof(rules));
	rules.rules = config.rules;
	rules.model = config.model;
	rules.layout = config.layout;
	rules.variant = config.variant;
	rules.options = config.options;
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context == NULL) {
		wlr_log(L_ERROR, "Cannot create XKB context");
		return NULL;
	}
	wlr_keyboard_set_keymap(device->keyboard, xkb_map_new_from_names(context,
		&rules, XKB_KEYMAP_COMPILE_NO_FLAGS));
	xkb_context_unref(context);

	return keyboard;
}

void roots_keyboard_destroy(struct wlr_input_device *device, struct roots_input *input) {
	struct roots_keyboard *keyboard = device->data;
	wl_list_remove(&keyboard->link);
	free(keyboard);
}
