#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_tool.h>

void wlr_tablet_tool_init(struct wlr_tablet_tool *tool,
		struct wlr_tablet_tool_impl *impl) {
	tool->impl = impl;
	wl_signal_init(&tool->events.axis);
	wl_signal_init(&tool->events.proximity);
	wl_signal_init(&tool->events.tip);
	wl_signal_init(&tool->events.button);
}

void wlr_tablet_tool_destroy(struct wlr_tablet_tool *tool) {
	if (!tool) {
		return;
	}

	wlr_list_for_each(&tool->paths, free);
	wlr_list_finish(&tool->paths);

	if (tool->impl && tool->impl->destroy) {
		tool->impl->destroy(tool);
	} else {
		free(tool);
	}
}
