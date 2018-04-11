#include <wlr/types/wlr_input_method.h>
#include "rootston/desktop.h"

void handle_input_panel_surface(struct wl_listener *listener, void *data);

void handle_input_method_context(struct wl_listener *listener, void *data);
void handle_input_method_context_destroy(struct wl_listener *listener,
	void *data);
