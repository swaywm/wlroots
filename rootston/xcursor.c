#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "rootston/xcursor.h"
#include "rootston/input.h"

const char *roots_xcursor_get_resize_name(uint32_t edges) {
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
