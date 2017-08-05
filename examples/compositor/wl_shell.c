#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <stdlib.h>
#include "compositor.h"

static void shell_surface_pong(struct wl_client *client, struct wl_resource
		*resource, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO: implement shell surface pong");
}

static void shell_surface_move(struct wl_client *client, struct wl_resource
		*resource, struct wl_resource *seat, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO: implement shell surface move");
}

static void shell_surface_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat, uint32_t serial,
		uint32_t edges) {
	wlr_log(L_DEBUG, "TODO: implement shell surface resize");
}

static void shell_surface_set_toplevel(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_toplevel");
}

static void shell_surface_set_transient(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent, int32_t x,
		int32_t y, uint32_t flags) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_transient");
}

static void shell_surface_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, uint32_t method, uint32_t framerate,
		struct wl_resource *output) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_fullscreen");
}

static void shell_surface_set_popup(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat, uint32_t serial,
		struct wl_resource *parent, int32_t x, int32_t y, uint32_t flags) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_popup");
}

static void shell_surface_set_maximized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_maximized");
}

static void shell_surface_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_title");
}

static void shell_surface_set_class(struct wl_client *client,
		struct wl_resource *resource, const char *class_) {
	wlr_log(L_DEBUG, "TODO: implement shell surface set_class");
}

struct wl_shell_surface_interface shell_surface_interface = {
	.pong = shell_surface_pong,
	.move = shell_surface_move,
	.resize = shell_surface_resize,
	.set_toplevel = shell_surface_set_toplevel,
	.set_transient = shell_surface_set_transient,
	.set_fullscreen = shell_surface_set_fullscreen,
	.set_popup = shell_surface_set_popup,
	.set_maximized = shell_surface_set_maximized,
	.set_title = shell_surface_set_title,
	.set_class = shell_surface_set_class,
};

struct shell_surface_state {
	struct wlr_surface *wlr_surface;
};

static void destroy_shell_surface(struct wl_resource *resource) {
	struct shell_surface_state *state = wl_resource_get_user_data(resource);
	free(state);
}

void wl_shell_get_shell_surface(struct wl_client *client,
				  struct wl_resource *resource, uint32_t id,
				  struct wl_resource *surface) {
	struct wlr_surface *wlr_surface = wl_resource_get_user_data(surface);
	struct shell_surface_state *state = malloc(sizeof(struct shell_surface_state));
	state->wlr_surface = wlr_surface;
	struct wl_resource *shell_surface_resource = wl_resource_create(client,
			&wl_shell_surface_interface, wl_resource_get_version(resource), id);
	wl_resource_set_implementation(shell_surface_resource,
			&shell_surface_interface, state, destroy_shell_surface);
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
