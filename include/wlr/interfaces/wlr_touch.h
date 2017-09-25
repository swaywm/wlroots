#ifndef WLR_INTERFACES_WLR_TOUCH_H
#define WLR_INTERFACES_WLR_TOUCH_H

#include <wlr/types/wlr_touch.h>

struct wlr_touch_impl {
	void (*destroy)(struct wlr_touch *touch);
};

void wlr_touch_init(struct wlr_touch *touch,
		struct wlr_touch_impl *impl);
void wlr_touch_destroy(struct wlr_touch *touch);

#endif
