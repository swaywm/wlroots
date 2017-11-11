#ifndef _ROOTSTON_XCURSOR_H
#define _ROOTSTON_XCURSOR_H

#include <wayland-server.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland.h>
#include <wlr/types/wlr_cursor.h>

#define ROOTS_XCURSOR_SIZE 16

struct roots_xcursor_scaled_theme {
	uint32_t scale;
	struct wlr_xcursor_theme *theme;
	struct wl_list link;
};

struct roots_xcursor_theme {
	char *name;
	struct wl_list scaled_themes; // roots_xcursor_scaled_theme::link
};

struct roots_xcursor_theme *roots_xcursor_theme_create(const char *name);

void roots_xcursor_theme_destroy(struct roots_xcursor_theme *theme);

int roots_xcursor_theme_load(struct roots_xcursor_theme *theme,
	uint32_t scale);

void roots_xcursor_theme_set_default(struct roots_xcursor_theme *theme,
	struct wlr_cursor *cursor);

void roots_xcursor_theme_set_move(struct roots_xcursor_theme *theme,
	struct wlr_cursor *cursor);

void roots_xcursor_theme_set_resize(struct roots_xcursor_theme *theme,
	struct wlr_cursor *cursor, uint32_t edges);

void roots_xcursor_theme_set_rotate(struct roots_xcursor_theme *theme,
	struct wlr_cursor *cursor);

void roots_xcursor_theme_xwayland_set_default(struct roots_xcursor_theme *theme,
	struct wlr_xwayland *xwayland);

#endif
