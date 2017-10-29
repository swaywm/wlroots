#include <wlr/types/wlr_cursor.h>
#include "rootston/input.h"

struct wlr_xcursor *get_default_xcursor(struct wlr_xcursor_theme *theme) {
	return wlr_xcursor_theme_get_cursor(theme, "left_ptr");
}

struct wlr_xcursor *get_move_xcursor(struct wlr_xcursor_theme *theme) {
	return wlr_xcursor_theme_get_cursor(theme, "grabbing");
}

static const char *get_resize_xcursor_name(uint32_t edges) {
	if (edges & ROOTS_CURSOR_RESIZE_EDGE_TOP) {
		if (edges & ROOTS_CURSOR_RESIZE_EDGE_RIGHT) {
			return "ne-resize";
		} else if (edges & ROOTS_CURSOR_RESIZE_EDGE_LEFT) {
			return "nw-resize";
		}
		return "n-resize";
	} else if (edges & ROOTS_CURSOR_RESIZE_EDGE_BOTTOM) {
		if (edges & ROOTS_CURSOR_RESIZE_EDGE_RIGHT) {
			return "se-resize";
		} else if (edges & ROOTS_CURSOR_RESIZE_EDGE_LEFT) {
			return "sw-resize";
		}
		return "s-resize";
	} else if (edges & ROOTS_CURSOR_RESIZE_EDGE_RIGHT) {
		return "e-resize";
	} else if (edges & ROOTS_CURSOR_RESIZE_EDGE_LEFT) {
		return "w-resize";
	}
	return "se-resize"; // fallback
}

struct wlr_xcursor *get_resize_xcursor(struct wlr_xcursor_theme *theme,
	uint32_t edges) {
	return wlr_xcursor_theme_get_cursor(theme, get_resize_xcursor_name(edges));
}

struct wlr_xcursor *get_rotate_xcursor(struct wlr_xcursor_theme *theme) {
	return wlr_xcursor_theme_get_cursor(theme, "grabbing");
}
