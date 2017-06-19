#ifndef _WLR_INTERFACES_POINTER_H
#define _WLR_INTERFACES_POINTER_H
#include <wlr/types/wlr_pointer.h>

struct wlr_pointer_impl {
	void (*destroy)(struct wlr_pointer_state *state);
};

struct wlr_pointer *wlr_pointer_create(struct wlr_pointer_impl *impl,
		struct wlr_pointer_state *state);
void wlr_pointer_destroy(struct wlr_pointer *pointer);

#endif
