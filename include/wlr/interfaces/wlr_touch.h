#ifndef _WLR_INTERFACES_TOUCH_H
#define _WLR_INTERFACES_TOUCH_H
#include <wlr/types/wlr_touch.h>

struct wlr_touch_impl {
	void (*destroy)(struct wlr_touch_state *state);
};

struct wlr_touch *wlr_touch_create(struct wlr_touch_impl *impl,
		struct wlr_touch_state *state);
void wlr_touch_destroy(struct wlr_touch *touch);

#endif
