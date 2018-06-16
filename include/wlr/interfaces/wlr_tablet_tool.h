#ifndef WLR_INTERFACES_WLR_TABLET_TOOL_H
#define WLR_INTERFACES_WLR_TABLET_TOOL_H

#include <wlr/types/wlr_tablet_tool.h>

struct wlr_tablet_impl {
	void (*destroy)(struct wlr_tablet *tablet);
};

void wlr_tablet_init(struct wlr_tablet *tablet,
		struct wlr_tablet_impl *impl);
void wlr_tablet_destroy(struct wlr_tablet *tablet);

#endif
