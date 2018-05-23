#ifndef ROOTSTON_TEXT_INPUT_H
#define ROOTSTON_TEXT_INPUT_H

#include <wlr/types/wlr_input_method.h>
#include <wlr/types/wlr_text_input.h>
#include "rootston/seat.h"

struct roots_text_input {
    struct roots_seat *seat;
    struct wlr_text_input *input;

	bool enabled; // client requested state
	bool sent_enter; // server requested state
	bool active; // active focus

    struct wl_list link;
    struct wl_listener text_input_enable;
    struct wl_listener text_input_disable;
    struct wl_listener text_input_commit;
    struct wl_listener input_method_commit_string;
    struct wl_listener input_method_preedit_string;
    struct wl_listener text_input_destroy;
};

struct roots_text_input *roots_text_input_create(struct roots_seat *seat,
    struct wlr_text_input *text_input);

void roots_text_input_destroy(struct roots_text_input *text_input);

#endif
