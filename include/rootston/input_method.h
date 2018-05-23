#include <wlr/types/wlr_input_method.h>
#include "rootston/desktop.h"

void handle_input_panel_surface(struct wl_listener *listener, void *data);

void handle_input_method_context(struct wl_listener *listener, void *data);
void handle_input_method_context_destroy(struct wl_listener *listener,
	void *data);

void input_method_manage_process(struct roots_desktop *desktop);
bool input_method_check_credentials(struct wl_client *client, void *data);
