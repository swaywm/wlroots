#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include "../compositor.h"

static void wl_compositor_create_surface(struct wl_client *client,
			   struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: implement create_surface");
}

static void wl_compositor_create_region(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: implement create_region");
}

struct wl_compositor_interface wl_compositor_impl = {
	.create_surface = wl_compositor_create_surface,
	.create_region = wl_compositor_create_region
};

static void wl_compositor_destroy(struct wl_resource *resource) {
	struct wl_compositor_state *state = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &state->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_compositor_bind(struct wl_client *wl_client, void *_state,
		uint32_t version, uint32_t id) {
	struct wl_compositor_state *state = _state;
	assert(wl_client && state);
	if (version > 4) {
		wlr_log(L_ERROR, "Client requested unsupported wl_compositor version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &wl_compositor_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_compositor_impl,
			state, wl_compositor_destroy);
	wl_list_insert(&state->wl_resources, wl_resource_get_link(wl_resource));
}

void wl_compositor_init(struct wl_display *display,
		struct wl_compositor_state *state) {
	struct wl_global *wl_global = wl_global_create(display,
		&wl_compositor_interface, 4, state, wl_compositor_bind);
	state->wl_global = wl_global;
	wl_list_init(&state->wl_resources);
}
