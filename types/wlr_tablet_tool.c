#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/interfaces/wlr_tablet_tool.h>

struct wlr_tablet_tool *wlr_tablet_tool_create(struct wlr_tablet_tool_impl *impl,
		struct wlr_tablet_tool_state *state) {
	struct wlr_tablet_tool *tool = calloc(1, sizeof(struct wlr_tablet_tool));
	tool->impl = impl;
	tool->state = state;
	wl_signal_init(&tool->events.axis);
	wl_signal_init(&tool->events.proximity);
	wl_signal_init(&tool->events.tip);
	wl_signal_init(&tool->events.button);
	return tool;
}

void wlr_tablet_tool_destroy(struct wlr_tablet_tool *tool) {
	if (!tool) return;
	if (tool->impl) {
		tool->impl->destroy(tool->state);
	}
	free(tool);
}
