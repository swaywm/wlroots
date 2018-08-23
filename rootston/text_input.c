#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "rootston/seat.h"
#include "rootston/text_input.h"

static struct roots_text_input *relay_get_focusable_text_input(
		struct roots_input_method_relay *relay) {
	struct roots_text_input *text_input = NULL;
	wl_list_for_each(text_input, &relay->text_inputs, link) {
		if (text_input->pending_focused_surface) {
			return text_input;
		}
	}
	return NULL;
}

static struct roots_text_input *relay_get_focused_text_input(
		struct roots_input_method_relay *relay) {
	struct roots_text_input *text_input = NULL;
	wl_list_for_each(text_input, &relay->text_inputs, link) {
		if (text_input->input->focused_surface) {
			return text_input;
		}
	}
	return NULL;
}

static void handle_im_commit(struct wl_listener *listener, void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_commit);

	struct roots_text_input *text_input = relay_get_focused_text_input(relay);
	if (!text_input) {
		return;
	}
	struct wlr_input_method_v2 *context = data;
	assert(context == relay->input_method);
	if (context->current.preedit.text) {
		wlr_text_input_v3_send_preedit_string(text_input->input,
			context->current.preedit.text,
			context->current.preedit.cursor_begin,
			context->current.preedit.cursor_end);
	}
	if (context->current.commit_text) {
		wlr_text_input_v3_send_commit_string(text_input->input,
			context->current.commit_text);
	}
	if (context->current.delete.before_length
			|| context->current.delete.after_length) {
		wlr_text_input_v3_send_delete_surrounding_text(text_input->input,
			context->current.delete.before_length,
			context->current.delete.after_length);
	}
	wlr_text_input_v3_send_done(text_input->input);
}

static void text_input_set_pending_focused_surface(
		struct roots_text_input *text_input, struct wlr_surface *surface) {
	text_input->pending_focused_surface = surface;
	wl_signal_add(&surface->events.destroy,
		&text_input->pending_focused_surface_destroy);
}

static void text_input_clear_pending_focused_surface(
		struct roots_text_input *text_input) {
	wl_list_remove(&text_input->pending_focused_surface_destroy.link);
	text_input->pending_focused_surface = NULL;
}

static void handle_im_destroy(struct wl_listener *listener, void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_destroy);
	struct wlr_input_method_v2 *context = data;
	assert(context == relay->input_method);
	relay->input_method = NULL;
	struct roots_text_input *text_input = relay_get_focused_text_input(relay);
	if (text_input) {
		// keyboard focus is still there, so keep the surface at hand in case
		// the input method returns
		text_input_set_pending_focused_surface(text_input,
			text_input->input->focused_surface);
		wlr_text_input_v3_send_leave(text_input->input);
	}
}

static void relay_send_im_done(struct roots_input_method_relay *relay,
		struct wlr_text_input_v3 *input) {
	struct wlr_input_method_v2 *input_method = relay->input_method;
	if (!input_method) {
		wlr_log(WLR_INFO, "Sending IM_DONE but im is gone");
		return;
	}
	// TODO: only send each of those if they were modified
	wlr_input_method_v2_send_surrounding_text(input_method,
		input->current.surrounding.text, input->current.surrounding.cursor,
		input->current.surrounding.anchor);
	wlr_input_method_v2_send_text_change_cause(input_method,
		input->current.text_change_cause);
	wlr_input_method_v2_send_content_type(input_method,
		input->current.content_type.hint, input->current.content_type.purpose);
	wlr_input_method_v2_send_done(input_method);
	// TODO: pass intent, display popup size
}

static struct roots_text_input *text_input_to_roots(
		struct roots_input_method_relay *relay,
		struct wlr_text_input_v3 *text_input) {
	struct roots_text_input *roots_text_input = NULL;
	wl_list_for_each(roots_text_input, &relay->text_inputs, link) {
		if (roots_text_input->input == text_input) {
			return roots_text_input;
		}
	}
	return NULL;
}

static void handle_text_input_enable(struct wl_listener *listener, void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		text_input_enable);
	if (relay->input_method == NULL) {
		wlr_log(WLR_INFO, "Enabling text input when input method is gone");
		return;
	}
	struct roots_text_input *text_input = text_input_to_roots(relay,
		(struct wlr_text_input_v3*)data);
	wlr_input_method_v2_send_activate(relay->input_method);
	relay_send_im_done(relay, text_input->input);
}

static void handle_text_input_commit(struct wl_listener *listener,
		void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		text_input_commit);
	struct roots_text_input *text_input = text_input_to_roots(relay,
		(struct wlr_text_input_v3*)data);
	if (!text_input->input->current_enabled) {
		wlr_log(WLR_INFO, "Inactive text input tried to commit an update");
		return;
	}
	wlr_log(WLR_DEBUG, "Text input committed update");
	if (relay->input_method == NULL) {
		wlr_log(WLR_INFO, "Text input committed, but input method is gone");
		return;
	}
	relay_send_im_done(relay, text_input->input);
}

static void relay_disable_text_input(struct roots_input_method_relay *relay,
		struct roots_text_input *text_input) {
	if (relay->input_method == NULL) {
		wlr_log(WLR_DEBUG, "Disabling text input, but input method is gone");
		return;
	}
	wlr_input_method_v2_send_deactivate(relay->input_method);
	relay_send_im_done(relay, text_input->input);
}

static void handle_text_input_disable(struct wl_listener *listener,
		void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		text_input_disable);
	struct roots_text_input *text_input = text_input_to_roots(relay,
		(struct wlr_text_input_v3*)data);
	if (!text_input->input->current_enabled) {
		wlr_log(WLR_DEBUG, "Inactive text input tried to disable itself");
		return;
	}
	relay_disable_text_input(relay, text_input);
}

static void handle_text_input_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		text_input_destroy);
	struct roots_text_input *text_input = text_input_to_roots(relay,
		(struct wlr_text_input_v3*)data);

	if (text_input->input->current_enabled) {
		relay_disable_text_input(relay, text_input);
	}

	wl_list_remove(&text_input->link);
	text_input->input = NULL;
	free(text_input);
}

static void handle_pending_focused_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		pending_focused_surface_destroy);
	struct wlr_surface *surface = data;
	assert(text_input->pending_focused_surface == surface);
	text_input->pending_focused_surface = NULL;
}

struct roots_text_input *roots_text_input_create(
		struct roots_input_method_relay *relay,
		struct wlr_text_input_v3 *text_input) {
	struct roots_text_input *input = calloc(1, sizeof(struct roots_text_input));
	if (!input) {
		return NULL;
	}
	input->input = text_input;
	input->relay = relay;

	wl_signal_add(&text_input->events.enable, &relay->text_input_enable);
	relay->text_input_enable.notify = handle_text_input_enable;

	wl_signal_add(&text_input->events.commit, &relay->text_input_commit);
	relay->text_input_commit.notify = handle_text_input_commit;

	wl_signal_add(&text_input->events.disable, &relay->text_input_disable);
	relay->text_input_disable.notify = handle_text_input_disable;

	wl_signal_add(&text_input->events.destroy, &relay->text_input_destroy);
	relay->text_input_destroy.notify = handle_text_input_destroy;

	input->pending_focused_surface_destroy.notify =
		handle_pending_focused_surface_destroy;
	return input;
}

static void relay_handle_text_input(struct wl_listener *listener,
		void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		text_input_new);
	struct wlr_text_input_v3 *wlr_text_input = data;
	if (relay->seat->seat != wlr_text_input->seat) {
		return;
	}

	struct roots_text_input *text_input = roots_text_input_create(relay,
		wlr_text_input);
	if (!text_input) {
		return;
	}
	wl_list_insert(&relay->text_inputs, &text_input->link);
}

static void relay_handle_input_method(struct wl_listener *listener,
		void *data) {
	struct roots_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_new);
	struct wlr_input_method_v2 *input_method = data;
	if (relay->seat->seat != input_method->seat) {
		return;
	}

	if (relay->input_method != NULL) {
		wlr_log(WLR_INFO, "Attempted to connect second input method to a seat");
		wlr_input_method_v2_send_unavailable(input_method);
		return;
	}

	relay->input_method = input_method;
	wl_signal_add(&relay->input_method->events.commit,
		&relay->input_method_commit);
	relay->input_method_commit.notify = handle_im_commit;
	wl_signal_add(&relay->input_method->events.destroy,
		&relay->input_method_destroy);
	relay->input_method_destroy.notify = handle_im_destroy;

	struct roots_text_input *text_input = relay_get_focusable_text_input(relay);
	if (text_input) {
		wlr_text_input_v3_send_enter(text_input->input,
			text_input->pending_focused_surface);
		text_input_clear_pending_focused_surface(text_input);
	}
}

void roots_input_method_relay_init(struct roots_seat *seat,
		struct roots_input_method_relay *relay) {
	relay->seat = seat;
	wl_list_init(&relay->text_inputs);

	relay->text_input_new.notify = relay_handle_text_input;
	wl_signal_add(&seat->input->server->desktop->text_input->events.text_input,
		&relay->text_input_new);

	relay->input_method_new.notify = relay_handle_input_method;
	wl_signal_add(
		&seat->input->server->desktop->input_method->events.input_method,
		&relay->input_method_new);
}

void roots_input_method_relay_set_focus(struct roots_input_method_relay *relay,
		struct wlr_surface *surface) {
	struct roots_text_input *text_input;
	wl_list_for_each(text_input, &relay->text_inputs, link) {
		if (text_input->pending_focused_surface) {
			assert(text_input->input->focused_surface == NULL);
			if (surface != text_input->pending_focused_surface) {
				text_input_clear_pending_focused_surface(text_input);
			}
		} else if (text_input->input->focused_surface) {
			assert(text_input->pending_focused_surface == NULL);
			if (surface != text_input->input->focused_surface) {
				relay_disable_text_input(relay, text_input);
				wlr_text_input_v3_send_leave(text_input->input);
			}
		} else if (surface
				&& wl_resource_get_client(text_input->input->resource)
				== wl_resource_get_client(surface->resource)) {
			if (relay->input_method) {
				wlr_text_input_v3_send_enter(text_input->input, surface);
			} else {
				text_input_set_pending_focused_surface(text_input, surface);
			}
		}
	}
}
