#include <assert.h>
#include <wayland-util.h>
#include <wlr/types/wlr_input_method.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "input-method-unstable-v1-protocol.h"
#include "util/signal.h"

static void noop() {}

static const struct zwp_input_panel_surface_v1_interface
	input_panel_surface_impl;

static struct wlr_input_panel_surface *panel_surface_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_panel_surface_v1_interface, &input_panel_surface_impl));
	return wl_resource_get_user_data(resource);
}

static const struct zwp_input_panel_surface_v1_interface
		input_panel_surface_impl = {
	.set_toplevel = noop,
	.set_overlay_panel = noop,
};


static void handle_input_panel_surface_destroy(struct wl_resource *resource) {
	struct wlr_input_panel_surface *surface =
		panel_surface_from_resource(resource);
	wlr_signal_emit_safe(&surface->events.destroy, surface);
	wl_list_remove(&surface->link);
	free(surface);
}

static const struct zwp_input_panel_v1_interface input_panel_impl;

static struct wlr_input_panel *panel_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_input_panel_v1_interface,
		&input_panel_impl));
	return wl_resource_get_user_data(resource);
}

static void get_input_panel_surface(struct wl_client *client,
		struct wl_resource *panel_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	if (wlr_surface_set_role(surface, "zwp-input-panel-role",
			panel_resource, WL_SHELL_ERROR_ROLE)) {
		return;
	}

	struct wl_resource *wl_resource = wl_resource_create(client,
		&zwp_input_panel_surface_v1_interface,
		wl_resource_get_version(panel_resource), id);
	if (wl_resource == NULL) {
		wl_resource_post_no_memory(panel_resource);
		return;
	}
	struct wlr_input_panel *panel = panel_from_resource(panel_resource);
	struct wlr_input_panel_surface *panel_surface =
		calloc(1, sizeof(struct wlr_input_panel_surface));
	if (panel_surface == NULL) {
		wl_resource_post_no_memory(panel_resource);
		return;
	}
	wl_signal_init(&panel_surface->events.destroy);
	panel_surface->surface = surface;
	panel_surface->panel = panel; // be able to tell the panel about changes
	wl_resource_set_implementation(wl_resource, &input_panel_surface_impl,
		panel_surface, handle_input_panel_surface_destroy);
	wlr_signal_emit_safe(&panel->events.new_surface, panel_surface);
}

static const struct zwp_input_panel_v1_interface input_panel_impl = {
	.get_input_panel_surface = get_input_panel_surface,
};

static void input_panel_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	assert(wl_client);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&zwp_input_panel_v1_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource, &input_panel_impl, data, NULL);
}

struct wlr_input_panel *wlr_input_panel_create(struct wl_display *display) {
	struct wlr_input_panel *panel = calloc(1, sizeof(struct wlr_input_panel));
	if (!panel) {
		return NULL;
	}
	panel->panel = wl_global_create(display, &zwp_input_panel_v1_interface, 1,
		panel, input_panel_bind);
	wl_signal_init(&panel->events.new_surface);
	wl_list_init(&panel->surfaces);
	return panel;
}

void commit_string(struct wl_client *client, struct wl_resource *resource,
		uint32_t serial, const char *text) {
	struct wlr_input_method_context *context =
		wl_resource_get_user_data(resource);
	wlr_signal_emit_safe(&context->events.commit_string, (void*)text);
}

void preedit_string(struct wl_client *client, struct wl_resource *resource,
		uint32_t serial, const char *text, const char *commit) {
	struct wlr_input_method_context *context =
		wl_resource_get_user_data(resource);
	const struct wlr_input_method_context_preedit_string preedit = {text,
		commit};
	wlr_signal_emit_safe(&context->events.preedit_string, (void*)&preedit);
}

uint32_t *get_code(struct wlr_input_method_context *context, uint32_t sym,
		uint32_t modifiers) {
	for (uint32_t i = 0; i < context->code_counts; i++) {
		if (context->symcodes[i].sym == sym) {
			return &context->symcodes[i].code;
		}
	}
	return NULL;
}

void input_method_keysym(struct wl_client *client, struct wl_resource *resource,
		uint32_t serial, uint32_t time, uint32_t sym, uint32_t state,
		uint32_t modifiers) {
	struct wlr_input_method_context *context =
		wl_resource_get_user_data(resource);
	//wlr_keyboard_notify_modifiers(&context->keyboard, 0, 0, 0, 0); // FIXME
	uint32_t *code = get_code(context, sym, modifiers);
	if (!code) {
		wlr_log(L_INFO, "No code corresponding to keysym %x", sym);
		return;
	}
	struct wlr_event_keyboard_key key_event = {
		.time_msec = time,
		.keycode = *code - 8,
		.update_state = false,
		.state = state ? WLR_KEY_PRESSED : WLR_KEY_RELEASED,
	};
	wlr_keyboard_notify_key(context->keyboard, &key_event);
}

static const struct zwp_input_method_context_v1_interface input_context_impl = {
	.destroy = noop,
	.commit_string = commit_string,
	.preedit_string = preedit_string,
	.preedit_styling = noop,
	.preedit_cursor = noop,
	.delete_surrounding_text = noop,
	.cursor_position = noop,
	.modifiers_map = noop,
	.keysym = input_method_keysym,
	.grab_keyboard = noop,
	.key = noop,
	.modifiers = noop,
	.language = noop,
	.text_direction = noop,
};

static struct wlr_input_method *wlr_input_method_from_resource(struct wl_resource *resource) {
	//assert(wl_resource_instance_of(resource, &wl_surface_interface,
		//&surface_interface)); // there is no interface to compare with
	return wl_resource_get_user_data(resource);
}

static void input_method_unbind(struct wl_resource *resource) {
	struct wlr_input_method *input_method = wlr_input_method_from_resource(resource);
	(void)input_method;
}

static void input_method_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	assert(wl_client);
	struct wlr_input_method *input_method = data;
	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&zwp_input_method_v1_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource, NULL, input_method, input_method_unbind);
	input_method->resource = wl_resource;
}

struct wlr_input_method *wlr_input_method_create(struct wl_display *display) {
	struct wlr_input_method *input_method = calloc(1,
		sizeof(struct wlr_input_method));
	if (!input_method) {
		return NULL;
	}
	wl_signal_init(&input_method->events.new_context);
	input_method->input_method = wl_global_create(display,
		&zwp_input_method_v1_interface, 1, input_method, input_method_bind);
	return input_method;
}

static void wlr_input_method_keyboard_set_leds(struct wlr_keyboard *wlr_kb,
		uint32_t leds) {
	wlr_log(L_DEBUG, "STUB: setting LEDs on input method to %x", leds);
}

static void wlr_input_method_keyboard_destroy(struct wlr_keyboard *wlr_kb) {
	// TODO: safe to ignore for now - keyboard will	be destroyed only iff
	// associated input method context is torn down, no need to tear down the
	// context
}

static void wlr_input_method_device_destroy(struct wlr_input_device *dev) {
}

struct wlr_keyboard_impl keyboard_impl = {
	.destroy = wlr_input_method_keyboard_destroy,
	.led_update = wlr_input_method_keyboard_set_leds
};

static const struct wlr_input_device_impl input_device_impl = {
	.destroy = wlr_input_method_device_destroy
};


static void count_keys(struct xkb_keymap *keymap, xkb_keycode_t key,
		void *data) {
	uint32_t *count = data;
	*count = *count + 1;
}

static void add_keys(struct xkb_keymap *keymap, xkb_keycode_t key,
		void *data) {
	struct symcode **code = data;
	struct xkb_state *state = xkb_state_new(keymap);
	const xkb_keysym_t sym = xkb_state_key_get_one_sym(state, key);

	struct symcode new_code = {
		.code = key,
		.sym = sym,
	};
	**code = new_code;
	*code = *code + 1;
	xkb_state_unref(state);
}

static bool context_keyboard_init(struct wlr_input_method_context *context) {
	struct wlr_keyboard* keyboard = calloc(1, sizeof(struct wlr_keyboard));
	if (keyboard == NULL) {
		wlr_log(L_ERROR, "Cannot allocate wlr_keyboard");
		goto fail_kb;
	}
	wlr_keyboard_init(keyboard, &keyboard_impl);

	struct xkb_rule_names rules = { 0 };
	struct xkb_context *xcb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (xcb_context == NULL) {
		wlr_log(L_ERROR, "Cannot create XKB context");
		goto fail_ctx;
	}

	struct xkb_keymap *keymap = xkb_map_new_from_names(xcb_context, &rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		wlr_log(L_ERROR, "Cannot create XKB keymap");
		goto fail_keymap;
	}
	wlr_keyboard_set_keymap(keyboard, keymap);

	uint32_t keycount = 0;
	xkb_keymap_key_for_each(keymap, count_keys, &keycount);
	struct symcode *codes = calloc(keycount, sizeof(struct symcode));
	if (codes == NULL) {
		wlr_log(L_ERROR, "Cannot allocate symcodes");
		goto fail_codes;
	}
	struct symcode *cursor = codes;
	xkb_keymap_key_for_each(keymap, add_keys, &cursor);

	xkb_keymap_unref(keymap);
	xkb_context_unref(xcb_context);

	uint32_t code_count = cursor - codes;

	wlr_input_device_init(&context->input_device, WLR_INPUT_DEVICE_KEYBOARD,
		&input_device_impl, "virtual keyboard", 0xabcd, 0x1234);

	context->symcodes = codes;
	context->code_counts = code_count;
	context->keyboard = keyboard;
	context->input_device.keyboard = context->keyboard;
	return true;
fail_codes:
	xkb_keymap_unref(keymap);
fail_keymap:
	xkb_context_unref(xcb_context);
fail_ctx:
	free(keyboard);
fail_kb:
	return false;
}

struct wlr_input_method_context *wlr_input_method_send_activate(
		struct wlr_input_method *input_method) {
	struct wlr_input_method_context *context = calloc(1,
		sizeof(struct wlr_input_method_context));
	if (!context) {
		return NULL;
	}
	wl_signal_init(&context->events.commit_string);
	wl_signal_init(&context->events.preedit_string);
	wl_signal_init(&context->events.destroy);
	struct wl_client *client = wl_resource_get_client(input_method->resource);
	int version = wl_resource_get_version(input_method->resource);
	struct wl_resource *wl_resource = wl_resource_create(client,
		&zwp_input_method_context_v1_interface, version, 0);
	if (wl_resource == NULL) {
		free(context);
		wl_client_post_no_memory(client);
		return NULL;
	}
	wl_resource_set_implementation(wl_resource, &input_context_impl, context,
		NULL);
	context->resource = wl_resource;
	context_keyboard_init(context);
	wlr_signal_emit_safe(&input_method->events.new_context, context);
	zwp_input_method_v1_send_activate(input_method->resource,
		context->resource);
	return context;
}

void wlr_input_method_send_deactivate(struct wlr_input_method *input_method,
		struct wlr_input_method_context *context) {
	zwp_input_method_v1_send_deactivate(input_method->resource,
		context->resource);
	wlr_signal_emit_safe(&context->events.destroy, context);
	// the resource must be destroyed after this call by the client, so proceed with freeing everything else
	wlr_input_device_destroy(&context->input_device);
	free(context);
}

void wlr_input_method_context_send_surrounding_text(
		struct wlr_input_method_context *input_method, const char *text,
		uint32_t cursor, uint32_t anchor) {
	if (text != NULL) {
		zwp_input_method_context_v1_send_surrounding_text(
			input_method->resource, text, cursor, anchor);
	}
}
