#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include "compositor.h"

void wl_shell_get_shell_surface(struct wl_client *client,
				  struct wl_resource *resource, uint32_t id,
				  struct wl_resource *surface) {
	wlr_log(L_DEBUG, "TODO: implement get_shell_surface");
}

static struct wl_shell_interface wl_shell_impl = {
	.get_shell_surface = wl_shell_get_shell_surface
};

static void wl_shell_destroy(struct wl_resource *resource) {
	struct wl_shell_state *state = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &state->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_shell_bind(struct wl_client *wl_client, void *_state,
		uint32_t version, uint32_t id) {
	struct wl_shell_state *state = _state;
	assert(wl_client && state);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported wl_shell version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &wl_shell_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_shell_impl,
			state, wl_shell_destroy);
	wl_list_insert(&state->wl_resources, wl_resource_get_link(wl_resource));
}

void wl_shell_init(struct wl_display *display,
		struct wl_shell_state *state) {
	struct wl_global *wl_global = wl_global_create(display,
		&wl_shell_interface, 1, state, wl_shell_bind);
	state->wl_global = wl_global;
	wl_list_init(&state->wl_resources);
}
