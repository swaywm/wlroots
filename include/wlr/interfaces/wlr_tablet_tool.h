#ifndef _WLR_INTERFACES_TABLET_TOOL_H
#define _WLR_INTERFACES_TABLET_TOOL_H
#include <wlr/types/wlr_tablet_tool.h>

struct wlr_tablet_tool_impl {
	void (*destroy)(struct wlr_tablet_tool_state *tool);
};

struct wlr_tablet_tool *wlr_tablet_tool_create(struct wlr_tablet_tool_impl *impl,
		struct wlr_tablet_tool_state *state);
void wlr_tablet_tool_destroy(struct wlr_tablet_tool *tool);

#endif
