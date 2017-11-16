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

static ssize_t pressed_keysyms_index(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	for (size_t i = 0; i < ROOTS_KEYBOARD_PRESSED_KEYSYMS_CAP; ++i) {
		if (pressed_keysyms[i] == keysym) {
			return i;
		}
	}
	return -1;
}

static void pressed_keysyms_add(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
	if (i < 0) {
		i = pressed_keysyms_index(pressed_keysyms, XKB_KEY_NoSymbol);
		if (i >= 0) {
			pressed_keysyms[i] = keysym;
		}
	}
}

static void pressed_keysyms_remove(xkb_keysym_t *pressed_keysyms,
		xkb_keysym_t keysym) {
	ssize_t i = pressed_keysyms_index(pressed_keysyms, keysym);
	if (i >= 0) {
		pressed_keysyms[i] = XKB_KEY_NoSymbol;
	}
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
			roots_seat_focus_view(keyboard->seat, view);
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
 * Execute a built-in, hardcoded compositor action when a keysym is pressed.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
 */
static bool keyboard_press_keysym(struct roots_keyboard *keyboard,
		xkb_keysym_t keysym) {
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

	return false;
}

/**
 * Press or release keysyms.
 *
 * Returns true if the keysym was handled by a binding and false if the event
 * should be propagated to clients.
 */
static bool keyboard_handle_keysyms(struct roots_keyboard *keyboard,
		xkb_keysym_t *pressed_keysyms, uint32_t modifiers,
		const xkb_keysym_t *keysyms, size_t keysyms_len,
		enum wlr_key_state state) {
	bool handled = false;
	for (size_t i = 0; i < keysyms_len; ++i) {
		if (state == WLR_KEY_PRESSED) {
			pressed_keysyms_add(pressed_keysyms, keysyms[i]);
			handled |= keyboard_press_keysym(keyboard, keysyms[i]);
		} else { // WLR_KEY_RELEASED
			pressed_keysyms_remove(pressed_keysyms, keysyms[i]);
		}
	}
	if (handled) {
		return true;
	}
	if (state != WLR_KEY_PRESSED) {
		return false;
	}

	struct wl_list *bindings = &keyboard->input->server->config->bindings;
	struct roots_binding_config *bc;
	wl_list_for_each(bc, bindings, link) {
		if (modifiers ^ bc->modifiers) {
			continue;
		}

		bool ok = true;
		for (size_t i = 0; i < bc->keysyms_len; i++) {
			ssize_t j = pressed_keysyms_index(pressed_keysyms, bc->keysyms[i]);
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

/*
 * Get keysyms and modifiers from the keyboard as xkb sees them.
 *
 * This uses the xkb keysyms translation based on pressed modifiers and clears
 * the consumed modifiers from the list of modifiers passed to keybind
 * detection.
 *
 * On US layout, this will trigger: Alt + @
 */
static size_t keyboard_keysyms_translated(struct roots_keyboard *keyboard,
		xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
		uint32_t *modifiers) {
	*modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
	xkb_mod_mask_t consumed = xkb_state_key_get_consumed_mods2(
		keyboard->device->keyboard->xkb_state, keycode, XKB_CONSUMED_MODE_XKB);
	*modifiers = *modifiers & ~consumed;

	return xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
		keycode, keysyms);
}

/*
 * Get keysyms and modifiers from the keyboard as if modifiers didn't change
 * keysyms.
 *
 * This avoids the xkb keysym translation based on modifiers considered pressed
 * in the state.
 *
 * This will trigger the keybind: Alt + Shift + 2
 */
static size_t keyboard_keysyms_raw(struct roots_keyboard *keyboard,
		xkb_keycode_t keycode, const xkb_keysym_t **keysyms,
		uint32_t *modifiers) {
	*modifiers = wlr_keyboard_get_modifiers(keyboard->device->keyboard);

	xkb_layout_index_t layout_index = xkb_state_key_get_layout(
		keyboard->device->keyboard->xkb_state, keycode);
	return xkb_keymap_key_get_syms_by_level(keyboard->device->keyboard->keymap,
		keycode, layout_index, 0, keysyms);
}

void roots_keyboard_handle_key(struct roots_keyboard *keyboard,
		struct wlr_event_keyboard_key *event) {
	xkb_keycode_t keycode = event->keycode + 8;

	uint32_t modifiers;
	const xkb_keysym_t *keysyms;
	size_t keysyms_len = keyboard_keysyms_translated(keyboard, keycode,
		&keysyms, &modifiers);
	bool handled = keyboard_handle_keysyms(keyboard,
		keyboard->pressed_keysyms_translated, modifiers, keysyms, keysyms_len,
		event->state);
	if (!handled) {
		keysyms_len = keyboard_keysyms_raw(keyboard, keycode, &keysyms,
			&modifiers);
		handled = keyboard_handle_keysyms(keyboard,
			keyboard->pressed_keysyms_raw, modifiers, keysyms, keysyms_len,
			event->state);
	}

	if (!handled) {
		wlr_seat_set_keyboard(keyboard->seat->seat, keyboard->device);
		wlr_seat_keyboard_notify_key(keyboard->seat->seat, event->time_msec,
			event->keycode, event->state);
	}
}

void roots_keyboard_handle_modifiers(struct roots_keyboard *r_keyboard) {
	struct wlr_seat *seat = r_keyboard->seat->seat;
	wlr_seat_set_keyboard(seat, r_keyboard->device);
	wlr_seat_keyboard_notify_modifiers(seat);
}

static void keyboard_config_merge(struct roots_keyboard_config *config,
		struct roots_keyboard_config *fallback) {
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
	if (config->meta_key == 0) {
		config->meta_key = fallback->meta_key;
	}
	if (config->name == NULL) {
		config->name = fallback->name;
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

	struct roots_keyboard_config *config =
		calloc(1, sizeof(struct roots_keyboard_config));
	if (config == NULL) {
		free(keyboard);
		return NULL;
	}
	keyboard_config_merge(config, roots_config_get_keyboard(input->config, device));
	keyboard_config_merge(config, roots_config_get_keyboard(input->config, NULL));

	struct roots_keyboard_config env_config = {
		.rules = getenv("XKB_DEFAULT_RULES"),
		.model = getenv("XKB_DEFAULT_MODEL"),
		.layout = getenv("XKB_DEFAULT_LAYOUT"),
		.variant = getenv("XKB_DEFAULT_VARIANT"),
		.options = getenv("XKB_DEFAULT_OPTIONS"),
	};
	keyboard_config_merge(config, &env_config);
	keyboard->config = config;

	struct xkb_rule_names rules;
	memset(&rules, 0, sizeof(rules));
	rules.rules = config->rules;
	rules.model = config->model;
	rules.layout = config->layout;
	rules.variant = config->variant;
	rules.options = config->options;
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

void roots_keyboard_destroy(struct roots_keyboard *keyboard) {
	wl_list_remove(&keyboard->link);
	free(keyboard->config);
	free(keyboard);
}
