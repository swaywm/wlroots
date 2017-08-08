#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <stdlib.h>
#include "compositor.h"
#include "compositor/protocols/xdg-shell.h"

static void xdg_surface_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO xdg surface destroy");
}

static void destroy_xdg_shell_surface(struct wl_resource *resource) {
	struct xdg_surface_state *state = wl_resource_get_user_data(resource);
	free(state);
}

static void xdg_surface_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "TODO xdg surface get toplevel");
}

static void xdg_surface_get_popup(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *parent,
		struct wl_resource *wl_positioner) {
	wlr_log(L_DEBUG, "TODO xdg surface get popup");
}


static void xdg_surface_ack_configure(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO xdg surface ack configure");
}
 
static void xdg_surface_set_window_geometry(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	wlr_log(L_DEBUG, "TODO xdg surface set window geometry");
}

static const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation = {
   .destroy = xdg_surface_destroy,
   .get_toplevel = xdg_surface_get_toplevel,
   .get_popup = xdg_surface_get_popup,
   .ack_configure = xdg_surface_ack_configure,
   .set_window_geometry = xdg_surface_set_window_geometry,
};

struct xdg_surface_state {
	struct wlr_surface *wlr_surface;
};

static void xdg_shell_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: xdg shell destroy");
}

static void xdg_shell_create_positioner(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: xdg shell create positioner");
}

static void xdg_shell_get_xdg_surface(struct wl_client *client, struct
		wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface *wlr_surface = wl_resource_get_user_data(surface_resource);
	struct xdg_surface_state *state = malloc(sizeof(struct xdg_surface_state));
	state->wlr_surface = wlr_surface;
	struct wl_resource *shell_surface_resource = wl_resource_create(client,
			&zxdg_surface_v6_interface, wl_resource_get_version(resource), id);
	wl_resource_set_implementation(shell_surface_resource,
			&zxdg_surface_v6_implementation, state, destroy_xdg_shell_surface);
}

static void xdg_shell_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO xdg shell pong");
}

static struct zxdg_shell_v6_interface xdg_shell_impl = {
   .destroy = xdg_shell_destroy,
   .create_positioner = xdg_shell_create_positioner,
   .get_xdg_surface = xdg_shell_get_xdg_surface,
   .pong = xdg_shell_pong,
};

static void xdg_destroy_shell(struct wl_resource *resource) {
	struct xdg_shell_state *state = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &state->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void xdg_shell_bind(struct wl_client *wl_client, void *_state,
		uint32_t version, uint32_t id) {
	struct xdg_shell_state *state = _state;
	assert(wl_client && state);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported wl_shell version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &zxdg_shell_v6_interface, version, id);
	wl_resource_set_implementation(wl_resource, &xdg_shell_impl,
			state, xdg_destroy_shell);
	wl_list_insert(&state->wl_resources, wl_resource_get_link(wl_resource));
}

void xdg_shell_init(struct wl_display *display, struct xdg_shell_state *state) {
	struct wl_global *wl_global = wl_global_create(display,
		&zxdg_shell_v6_interface, 1, state, xdg_shell_bind);
	state->wl_global = wl_global;
	wl_list_init(&state->wl_resources);
}
