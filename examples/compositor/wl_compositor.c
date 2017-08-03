#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include "compositor.h"

static void surface_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void surface_attach(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *buffer_resource, int32_t sx, int32_t sy) {
	wlr_log(L_DEBUG, "TODO: surface attach");
}

static void surface_damage(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	wlr_log(L_DEBUG, "TODO: surface damage");
}

static void surface_frame(struct wl_client *client,
		struct wl_resource *resource, uint32_t callback) {
	wlr_log(L_DEBUG, "TODO: surface frame");
}

static void surface_set_opaque_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {
	wlr_log(L_DEBUG, "TODO: surface opaque region");
}

static void surface_set_input_region(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *region_resource) {

	wlr_log(L_DEBUG, "TODO: surface input region");
}

static void surface_commit(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: surface surface commit");
}

static void surface_set_buffer_transform(struct wl_client *client,
		struct wl_resource *resource, int transform) {
	wlr_log(L_DEBUG, "TODO: surface surface buffer transform");
}

static void surface_set_buffer_scale(struct wl_client *client,
		struct wl_resource *resource,
		int32_t scale) {
	wlr_log(L_DEBUG, "TODO: surface set buffer scale");
}


static void surface_damage_buffer(struct wl_client *client,
		struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width,
		int32_t height) {
	wlr_log(L_DEBUG, "TODO: surface damage buffer");
}

struct wl_surface_interface surface_interface = {
	surface_destroy,
	surface_attach,
	surface_damage,
	surface_frame,
	surface_set_opaque_region,
	surface_set_input_region,
	surface_commit,
	surface_set_buffer_transform,
	surface_set_buffer_scale,
	surface_damage_buffer
};

static void destroy_surface(struct wl_resource *resource) {
	wlr_log(L_DEBUG, "TODO: destroy surface");
}

static void wl_compositor_create_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wl_compositor_state *state = wl_resource_get_user_data(resource);
	struct wl_resource *surface_resource = wl_resource_create(client,
			&wl_surface_interface, wl_resource_get_version(resource), id);
	struct wlr_surface *surface = wlr_render_surface_init(state->renderer);
	wl_resource_set_implementation(surface_resource, &surface_interface,
			surface, destroy_surface);
	wl_resource_set_user_data(surface_resource, surface);

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
		struct wl_compositor_state *state, struct wlr_renderer *renderer) {
	struct wl_global *wl_global = wl_global_create(display,
		&wl_compositor_interface, 4, state, wl_compositor_bind);
	state->wl_global = wl_global;
	state->renderer = renderer;
	wl_list_init(&state->wl_resources);
}
