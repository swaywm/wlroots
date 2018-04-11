#include <stdlib.h>
#include "rootston/text_input.h"
#include "rootston/desktop.h"

static void handle_preedit_string(struct wl_listener *listener, void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		input_method_preedit_string);
	struct wlr_input_method_context_preedit_string *preedit = data;
	wlr_text_input_send_preedit_string(text_input->input, preedit->text, 0);
}

static void handle_commit_string(struct wl_listener *listener, void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		input_method_commit_string);
	char *text = data;
	wlr_text_input_send_preedit_string(text_input->input, "", 0);
	wlr_text_input_send_commit_string(text_input->input, text);
}

static void handle_text_input_enable(struct wl_listener *listener, void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_enable);
	//uint32_t show_input_panel = *(uint32_t*)data;
	if (text_input->context != NULL) {
		return;
	}
	struct roots_desktop *desktop = text_input->seat->input->server->desktop;
	if (!desktop->input_method->resource) {
		return;
	}
	struct wlr_input_method_context *context =
		wlr_input_method_send_activate(desktop->input_method);
	text_input->context = context;
	// FIXME: show all panel surfaces
	wl_signal_add(&context->events.preedit_string,
		&text_input->input_method_preedit_string);
	text_input->input_method_preedit_string.notify = handle_preedit_string;
	wl_signal_add(&context->events.commit_string,
		&text_input->input_method_commit_string);
	text_input->input_method_commit_string.notify = handle_commit_string;
	// FIXME: handle destroy
}


static void handle_text_input_commit(struct wl_listener *listener,
		void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_commit);
	struct wlr_text_input *input = data;
	wlr_input_method_context_send_surrounding_text(text_input->context,
		input->current.surrounding.text, input->current.surrounding.cursor,
		input->current.surrounding.anchor);
}

static void handle_text_input_disable(struct wl_listener *listener,
		void *data) {
	struct roots_text_input *text_input = wl_container_of(listener, text_input,
		text_input_disable);
	if (text_input->context == NULL) {
		return;
	}
	struct roots_desktop *desktop = text_input->seat->input->server->desktop;
	wlr_input_method_send_deactivate(desktop->input_method, text_input->context);
	text_input->context = NULL;
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
