#ifndef WLR_INTERFACES_WLR_TABLET_TOOL_H
#define WLR_INTERFACES_WLR_TABLET_TOOL_H

#include <wlr/types/wlr_tablet_tool.h>

struct wlr_tablet_tool_impl {
	void (*destroy)(struct wlr_tablet_tool *tool);
};

void wlr_tablet_tool_init(struct wlr_tablet_tool *tool,
		struct wlr_tablet_tool_impl *impl);
void wlr_tablet_tool_destroy(struct wlr_tablet_tool *tool);

#endif
