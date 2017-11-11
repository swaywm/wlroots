#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "rootston/xcursor.h"
#include "rootston/input.h"

struct roots_xcursor_theme *roots_xcursor_theme_create(const char *name) {
	struct roots_xcursor_theme *theme =
		calloc(1, sizeof(struct roots_xcursor_theme));
	if (theme == NULL) {
		return NULL;
	}
	theme->name = strdup(name);
	wl_list_init(&theme->scaled_themes);
	return theme;
}

void roots_xcursor_theme_destroy(struct roots_xcursor_theme *theme) {
	if (theme == NULL) {
		return;
	}
	struct roots_xcursor_scaled_theme *scaled_theme, *tmp;
	wl_list_for_each_safe(scaled_theme, tmp, &theme->scaled_themes, link) {
		wl_list_remove(&scaled_theme->link);
		wlr_xcursor_theme_destroy(scaled_theme->theme);
		free(scaled_theme);
	}
	free(theme->name);
	free(theme);
}

int roots_xcursor_theme_load(struct roots_xcursor_theme *theme,
		uint32_t scale) {
	struct roots_xcursor_scaled_theme *scaled_theme;
	wl_list_for_each(scaled_theme, &theme->scaled_themes, link) {
		if (scaled_theme->scale == scale) {
			return 0;
		}
	}

	scaled_theme = calloc(1, sizeof(struct roots_xcursor_scaled_theme));
	if (scaled_theme == NULL) {
		return 1;
	}
	scaled_theme->scale = scale;
	scaled_theme->theme = wlr_xcursor_theme_load(NULL,
		ROOTS_XCURSOR_SIZE * scale);
	if (scaled_theme->theme == NULL) {
		free(scaled_theme);
		return 1;
	}
	wl_list_insert(&theme->scaled_themes, &scaled_theme->link);
	return 0;
}

static void roots_xcursor_theme_set(struct roots_xcursor_theme *theme,
		struct wlr_cursor *cursor, const char *name) {
	struct roots_xcursor_scaled_theme *scaled_theme;
	wl_list_for_each(scaled_theme, &theme->scaled_themes, link) {
		struct wlr_xcursor *xcursor =
			wlr_xcursor_theme_get_cursor(scaled_theme->theme, name);
		if (xcursor == NULL) {
			continue;
		}

		struct wlr_xcursor_image *image = xcursor->images[0];
		wlr_cursor_set_image(cursor, image->buffer, image->width,
			image->width, image->height, image->hotspot_x, image->hotspot_y,
			scaled_theme->scale);
	}
}

void roots_xcursor_theme_set_default(struct roots_xcursor_theme *theme,
		struct wlr_cursor *cursor) {
	roots_xcursor_theme_set(theme, cursor, "left_ptr");
}

void roots_xcursor_theme_set_move(struct roots_xcursor_theme *theme,
		struct wlr_cursor *cursor) {
	roots_xcursor_theme_set(theme, cursor, "grabbing");
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

void roots_xcursor_theme_set_resize(struct roots_xcursor_theme *theme,
		struct wlr_cursor *cursor, uint32_t edges) {
	roots_xcursor_theme_set(theme, cursor, get_resize_xcursor_name(edges));
}

void roots_xcursor_theme_set_rotate(struct roots_xcursor_theme *theme,
	struct wlr_cursor *cursor) {
	roots_xcursor_theme_set(theme, cursor, "grabbing");
}

void roots_xcursor_theme_xwayland_set_default(struct roots_xcursor_theme *theme,
		struct wlr_xwayland *xwayland) {
	struct roots_xcursor_scaled_theme *scaled_theme;
	wl_list_for_each(scaled_theme, &theme->scaled_themes, link) {
		if (scaled_theme->scale == 1) {
			struct wlr_xcursor *xcursor =
				wlr_xcursor_theme_get_cursor(scaled_theme->theme, "left_ptr");
			if (xcursor == NULL) {
				continue;
			}

			struct wlr_xcursor_image *image = xcursor->images[0];
			wlr_xwayland_set_cursor(xwayland, image->buffer, image->width,
				image->width, image->height, image->hotspot_x,
				image->hotspot_y);
			break;
		}
	}
}
