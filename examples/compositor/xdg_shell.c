#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <stdlib.h>
#include "compositor.h"
#include "compositor/protocols/xdg-shell.h"

static void resource_destructor(struct wl_client *client,
		struct wl_resource *resource) {
   wl_resource_destroy(resource);
}

static void xdg_toplevel_set_parent(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource) {
	wlr_log(L_DEBUG, "TODO: toplevel set parent");
}

static void xdg_toplevel_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	wlr_log(L_DEBUG, "TODO: toplevel set title");
}

static void xdg_toplevel_set_app_id(struct wl_client *client,
		struct wl_resource *resource, const char *app_id) {
	wlr_log(L_DEBUG, "TODO: toplevel set app id");
}

static void xdg_toplevel_show_window_menu(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat, uint32_t serial,
		int32_t x, int32_t y) {
	wlr_log(L_DEBUG, "TODO: toplevel show window menu");
}

static void xdg_toplevel_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO: toplevel move");
}

static void xdg_toplevel_resize(struct wl_client *client, struct wl_resource
		*resource, struct wl_resource *seat_resource, uint32_t serial,
		uint32_t edges) {
	wlr_log(L_DEBUG, "TODO: toplevel resize");
}

static void xdg_toplevel_set_max_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	wlr_log(L_DEBUG, "TODO: toplevel set max size");
}

static void xdg_toplevel_set_min_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	wlr_log(L_DEBUG, "TODO: toplevel set min size");
}

static void xdg_toplevel_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: toplevel set maximized");
}

static void xdg_toplevel_unset_maximized(struct wl_client *client, struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: toplevel unset maximized");
}

static void xdg_toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource) {
	wlr_log(L_DEBUG, "TODO: toplevel set fullscreen");
}

static void xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: toplevel unset fullscreen");
}

static void xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: toplevel set minimized");
}

static const struct zxdg_toplevel_v6_interface zxdg_toplevel_v6_implementation = {
	.destroy = resource_destructor,
	.set_parent = xdg_toplevel_set_parent,
	.set_title = xdg_toplevel_set_title,
	.set_app_id = xdg_toplevel_set_app_id,
	.show_window_menu = xdg_toplevel_show_window_menu,
	.move = xdg_toplevel_move,
	.resize = xdg_toplevel_resize,
	.set_max_size = xdg_toplevel_set_max_size,
	.set_min_size = xdg_toplevel_set_min_size,
	.set_maximized = xdg_toplevel_set_maximized,
	.unset_maximized = xdg_toplevel_unset_maximized,
	.set_fullscreen = xdg_toplevel_set_fullscreen,
	.unset_fullscreen = xdg_toplevel_unset_fullscreen,
	.set_minimized = xdg_toplevel_set_minimized
};

struct xdg_surface_state {
	struct wlr_texture *wlr_texture;
	struct wl_display *display;
};


static void destroy_xdg_shell_surface(struct wl_resource *resource) {
	struct xdg_surface_state *state = wl_resource_get_user_data(resource);
	free(state);
}

static void xdg_surface_get_toplevel(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct xdg_surface_state *state = wl_resource_get_user_data(resource);
	struct wl_resource *toplevel_resource = wl_resource_create(client,
			&zxdg_toplevel_v6_interface, wl_resource_get_version(resource), id);
	wl_resource_set_implementation(toplevel_resource,
			&zxdg_toplevel_v6_implementation, state, NULL);
    zxdg_surface_v6_send_configure(resource, wl_display_next_serial(state->display));
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
   .destroy = resource_destructor,
   .get_toplevel = xdg_surface_get_toplevel,
   .get_popup = xdg_surface_get_popup,
   .ack_configure = xdg_surface_ack_configure,
   .set_window_geometry = xdg_surface_set_window_geometry,
};

static void xdg_shell_create_positioner(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: xdg shell create positioner");
}

static void xdg_shell_get_xdg_surface(struct wl_client *client, struct
		wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct xdg_shell_state *shell_state = wl_resource_get_user_data(resource);
	struct wlr_texture *wlr_texture = wl_resource_get_user_data(surface_resource);
	wlr_log(L_DEBUG, "@@ MALLOC STATE");
	struct xdg_surface_state *state = malloc(sizeof(struct xdg_surface_state));
	state->display = shell_state->display;
	state->wlr_texture = wlr_texture;
	struct wl_resource *shell_surface_resource = wl_resource_create(client,
			&zxdg_surface_v6_interface, wl_resource_get_version(resource), id);
	wl_resource_set_implementation(shell_surface_resource,
			&zxdg_surface_v6_implementation, state, destroy_xdg_shell_surface);
}

static void xdg_shell_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO xdg shell pong");
}

static struct zxdg_shell_v6_interface xdg_shell_impl = {
   .destroy = resource_destructor,
   .create_positioner = xdg_shell_create_positioner,
   .get_xdg_surface = xdg_shell_get_xdg_surface,
   .pong = xdg_shell_pong,
};

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
			state, NULL);
	wl_list_insert(&state->wl_resources, wl_resource_get_link(wl_resource));
}

void xdg_shell_init(struct wl_display *display, struct xdg_shell_state *state) {
	struct wl_global *wl_global = wl_global_create(display,
		&zxdg_shell_v6_interface, 1, state, xdg_shell_bind);
	state->wl_global = wl_global;
	state->display = display;
	wl_list_init(&state->wl_resources);
}

void xdg_shell_release(struct xdg_shell_state *state) {
	if (!state)
		return;

	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &state->wl_resources) {
		struct wl_list *link = wl_resource_get_link(_resource);
		wl_list_remove(link);
	}
}
