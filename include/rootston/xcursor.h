#ifndef _ROOTSTON_XCURSOR_H
#define _ROOTSTON_XCURSOR_H

#include <wlr/xcursor.h>

struct wlr_xcursor *get_default_xcursor(struct wlr_xcursor_theme *theme);

struct wlr_xcursor *get_move_xcursor(struct wlr_xcursor_theme *theme);

struct wlr_xcursor *get_resize_xcursor(struct wlr_xcursor_theme *theme,
	uint32_t edges);

struct wlr_xcursor *get_rotate_xcursor(struct wlr_xcursor_theme *theme);

#endif
