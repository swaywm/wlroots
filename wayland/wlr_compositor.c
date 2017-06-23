#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/wayland/wlr_compositor.h>
#include <wlr/util/log.h>

static void wl_compositor_create_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "Creating surface for client");
}

static void wl_compositor_create_region(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	wlr_log(L_DEBUG, "Creating region for client");
}

static struct wl_compositor_interface wl_compositor_impl = {
	.create_surface = wl_compositor_create_surface,
	.create_region = wl_compositor_create_region,
};

static void wl_compositor_destroy(struct wl_resource *wl_resource) {
	struct wlr_compositor *wlr_c = wl_resource_get_user_data(wl_resource);
	struct wl_resource *_wl_resource = NULL;
	wl_resource_for_each(_wl_resource, &wlr_c->wl_resources) {
		if (_wl_resource == wl_resource) {
			struct wl_list *link = wl_resource_get_link(_wl_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_compositor_bind(struct wl_client *wl_client,
		void *_wlr_compositor, uint32_t version, uint32_t id) {
	struct wlr_compositor *wlr_c = _wlr_compositor;
	assert(wl_client && wlr_c);
	if (version > 3) {
		wlr_log(L_ERROR, "Client requested unsupported wl_compositor version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &wl_compositor_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_compositor_impl,
			wlr_c, wl_compositor_destroy);
	wl_list_insert(&wlr_c->wl_resources, wl_resource_get_link(wl_resource));
}

struct wlr_compositor *wlr_compositor_init(struct wl_display *display) {
	struct wlr_compositor *wlr_c = calloc(1, sizeof(struct wlr_compositor));
	struct wl_global *wl_global = wl_global_create(display,
		&wl_compositor_interface, 1, wlr_c, wl_compositor_bind);
	wlr_c->wl_global = wl_global;
	wl_list_init(&wlr_c->wl_resources);
	wl_signal_init(&wlr_c->events.bound);
	wl_signal_init(&wlr_c->events.create_surface);
	wl_signal_init(&wlr_c->events.create_region);
	return wlr_c;
}
