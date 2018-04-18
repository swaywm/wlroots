#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_tool.h>

void wlr_tablet_tool_tool_init(struct wlr_tablet_tool_tool *tool) {
	// Intentionaly empty (for now)
}

void wlr_tablet_tool_tool_destroy(struct wlr_tablet_tool_tool *tool) {
	if (!tool) {
		return;
	}
	free(tool);
}
