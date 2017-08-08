/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This is adapted from wayland-cursor, but with the wl_shm client stuff removed
 * so we can use it on the compositor, too.
 */
#ifndef _WLR_XCURSOR_H
#define _WLR_XCURSOR_H
#include <stdint.h>

struct wlr_cursor_image {
	uint32_t width;		/* actual width */
	uint32_t height;	/* actual height */
	uint32_t hotspot_x;	/* hot spot x (must be inside image) */
	uint32_t hotspot_y;	/* hot spot y (must be inside image) */
	uint32_t delay;		/* animation delay to next frame (ms) */
	uint8_t *buffer;
};

struct wlr_cursor {
	unsigned int image_count;
	struct wlr_cursor_image **images;
	char *name;
	uint32_t total_delay; /* length of the animation in ms */
};

struct wlr_cursor_theme {
	unsigned int cursor_count;
	struct wlr_cursor **cursors;
	char *name;
	int size;
};

struct wlr_cursor_theme *wlr_cursor_theme_load(const char *name, int size);

void wlr_cursor_theme_destroy(struct wlr_cursor_theme *theme);

struct wlr_cursor *wlr_cursor_theme_get_cursor(
		struct wlr_cursor_theme *theme, const char *name);

int wlr_cursor_frame(struct wlr_cursor *cursor, uint32_t time);

#endif
