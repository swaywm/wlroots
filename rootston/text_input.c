#include <assert.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "rootston/text_input.h"
#include "rootston/desktop.h"

static void handle_preedit_string(struct wl_listener *listener, void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		input_method_preedit_string);
	struct wlr_input_method_context_preedit_string *preedit = data;
	uint32_t cursor = 0;
	if (preedit->state->cursor_set && preedit->state->cursor > 0) {
		cursor = preedit->state->cursor;
	}
	wlr_text_input_send_preedit_string(text_input->input, preedit->text,
		cursor);
}

static void handle_commit_string(struct wl_listener *listener, void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		input_method_commit_string);
	char *text = data;
	wlr_text_input_send_preedit_string(text_input->input, "", 0);
	wlr_text_input_send_commit_string(text_input->input, text);
}


static void seat_create_input_method_context(struct roots_seat *seat,
		struct roots_text_input *text_input) {
	assert(seat == text_input->seat);
	struct roots_desktop *desktop = seat->input->server->desktop;
	struct wlr_input_method_context *context =
		wlr_input_method_send_activate(desktop->input_method);
	if (!context) {
		wlr_log(L_ERROR, "Couldn't create input method context");
		return;
	}
	wl_signal_add(&context->events.preedit_string,
				  &text_input->input_method_preedit_string);
	text_input->input_method_preedit_string.notify = handle_preedit_string;
	wl_signal_add(&context->events.commit_string,
				  &text_input->input_method_commit_string);
	text_input->input_method_commit_string.notify = handle_commit_string;
	text_input->active = true;
	seat->context = context;
// FIXME: handle destroy?
}

static void seat_destroy_input_method_context(struct roots_seat *seat,
		struct roots_text_input *text_input) {
	assert(seat == text_input->seat);
	struct roots_desktop *desktop = seat->input->server->desktop;
	wlr_input_method_send_deactivate(desktop->input_method,
		text_input->seat->context);
	text_input->active = false;
	seat->context = NULL;
}

static void seat_update_input_method_state(struct roots_seat *seat,
		struct roots_text_input *changed_text_input) {
	assert(seat == changed_text_input->seat);
	struct roots_desktop *desktop = seat->input->server->desktop;
	if (!desktop->input_method->resource) { // FIXME: use seat
		return;
	}

	struct roots_text_input *active_text_input = NULL;
	struct roots_text_input *cur_text_input;
	wl_list_for_each(cur_text_input, &seat->text_inputs, link) {
		if (cur_text_input->active) {
			active_text_input = cur_text_input;
			break;
		}
	}

	if (changed_text_input->enabled) {
		if (changed_text_input != active_text_input) {
			if (active_text_input) {
				seat_destroy_input_method_context(seat, active_text_input);
			}
			seat_create_input_method_context(seat, changed_text_input);
		}
	} else if (changed_text_input->active) {
		seat_destroy_input_method_context(seat, changed_text_input);
	}
}

static void handle_text_input_enable(struct wl_listener *listener, void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_enable);
	//uint32_t show_input_panel = *(uint32_t*)data;
	text_input->enabled = true;
	seat_update_input_method_state(text_input->seat, text_input);
}

static void handle_text_input_commit(struct wl_listener *listener,
		void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_commit);
	struct wlr_text_input *input = data;
	wlr_input_method_context_send_surrounding_text(text_input->seat->context,
		input->current.surrounding.text, input->current.surrounding.cursor,
		input->current.surrounding.anchor);
}

static void handle_text_input_disable(struct wl_listener *listener,
		void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_disable);
	text_input->enabled = false;
	if (text_input->seat->context == NULL) {
		return;
	}
	seat_update_input_method_state(text_input->seat, text_input);
}

static void handle_text_input_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_destroy);
	wl_list_remove(&text_input->link);
	roots_text_input_destroy(text_input);
	// FIXME: deallocate things?
}

struct roots_text_input *roots_text_input_create(struct roots_seat *seat,
		struct wlr_text_input *text_input) {
	struct roots_text_input *input = calloc(1, sizeof(struct roots_text_input));
	if (!input) {
		return NULL;
	}
	input->input = text_input;
	input->seat = seat;
	wl_signal_add(&text_input->events.enable, &input->text_input_enable);
	input->text_input_enable.notify = handle_text_input_enable;
	wl_signal_add(&text_input->events.commit, &input->text_input_commit);
	input->text_input_commit.notify = handle_text_input_commit;
	wl_signal_add(&text_input->events.disable, &input->text_input_disable);
	input->text_input_disable.notify = handle_text_input_disable;
	wl_signal_add(&text_input->events.destroy, &input->text_input_destroy);
	input->text_input_destroy.notify = handle_text_input_destroy;
	return input;
}

void roots_text_input_destroy(struct roots_text_input *text_input) {
}
