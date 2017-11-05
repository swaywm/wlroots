#include <stdlib.h>
#include <wlr/util/log.h>
#include "rootston/cursor.h"

struct roots_cursor *roots_cursor_create(struct roots_seat *seat) {
	struct roots_cursor *cursor = calloc(1, sizeof(struct roots_cursor));
	if (!cursor) {
		return NULL;
	}
	cursor->cursor = wlr_cursor_create();
	if (!cursor->cursor) {
		return NULL;
	}

	return cursor;
}

void roots_cursor_destroy(struct roots_cursor *cursor) {
	// TODO
}

void roots_cursor_handle_motion(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle motion");
}

void roots_cursor_handle_motion_absolute(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion_absolute *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle motion absolute");
}

void roots_cursor_handle_button(struct roots_cursor *cursor,
		struct wlr_event_pointer_button *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle button");
}

void roots_cursor_handle_axis(struct roots_cursor *cursor,
		struct wlr_event_pointer_axis *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle axis");
}

void roots_cursor_handle_touch_down(struct roots_cursor *cursor,
		struct wlr_event_touch_down *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle touch down");
}

void roots_cursor_handle_touch_up(struct roots_cursor *cursor,
		struct wlr_event_touch_up *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle touch up");
}

void roots_cursor_handle_touch_motion(struct roots_cursor *cursor,
		struct wlr_event_touch_motion *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle touch motion");
}

void roots_cursor_handle_tool_axis(struct roots_cursor *cursor,
		struct wlr_event_tablet_tool_axis *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle tool axis");
}

void roots_cursor_handle_tool_tip(struct roots_cursor *cursor,
		struct wlr_event_tablet_tool_tip *event) {
	wlr_log(L_DEBUG, "TODO: cursor handle tool tip");
}
