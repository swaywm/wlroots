#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xwayland_keyboard_grab_v1.h>

#include <xwayland-keyboard-grab-unstable-v1-protocol.h>

struct wlr_xwayland_keyboard_grab_v1_manager {
	struct wl_resource *resource;
	struct wlr_xwayland_keyboard_grab_v1 *xwayland_keyboard_grab;

	struct wl_list link; // wlr_xwayland_keyboard_grab_v1::clients;
	struct wl_list grabs;

	struct {
		struct wl_signal destroy;
	} events;
};

static void xwayland_keyboard_grab_grab_handle_destroy(struct wl_resource *resource) {
	assert(resource);
	struct wlr_xwayland_keyboard_grab_v1_grab *grab =
		wl_resource_get_user_data(resource);
	assert(grab);

	wl_signal_emit(&grab->events.destroy, grab);
	wl_list_remove(&grab->link);
	free(grab);
}

static void xwayland_keyboard_grab_grab_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_xwayland_keyboard_grab_v1_interface xwayland_keyboard_grab_impl = {
	.destroy = xwayland_keyboard_grab_grab_destroy,
};

static void xwayland_keyboard_grab_grab_keyboard(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface, struct wl_resource *seat) {
	assert(client);
	assert(resource);
	assert(surface);
	assert(seat);

	struct wlr_xwayland_keyboard_grab_v1_grab *grab =
		calloc(1, sizeof(struct wlr_xwayland_keyboard_grab_v1_grab));
	if (!grab) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *grab_resource = wl_resource_create(client,
		&zwp_xwayland_keyboard_grab_v1_interface, 1, id);
	if (!grab_resource) {
		wl_client_post_no_memory(client);
		free(grab);
		return;
	}
	grab->resource = grab_resource;
	wl_signal_init(&grab->events.destroy);

	struct wlr_seat_client *seat_client = wl_resource_get_user_data(seat);
	grab->seat = seat_client->seat;
	wlr_log(L_DEBUG, "Seat class: %s", wl_resource_get_class(seat));
	assert(grab->seat);

	grab->surface = wl_resource_get_user_data(surface);
	assert(grab->surface);

	wl_resource_set_implementation(grab_resource, &xwayland_keyboard_grab_impl,
		grab, &xwayland_keyboard_grab_grab_handle_destroy);

	struct wlr_xwayland_keyboard_grab_v1_manager *manager =
		wl_resource_get_user_data(resource);
	assert(manager);

	wl_list_insert(&manager->grabs, &grab->link);
	wlr_log(L_DEBUG, "Someting aquired an xwayland keyboard grab: %p", manager);
	wl_signal_emit(&manager->xwayland_keyboard_grab->events.new_grab, grab);
}

static void xwayland_keyboard_grab_manager_handle_destroy(struct wl_resource *resource) {
	assert(resource);
	struct wlr_xwayland_keyboard_grab_v1_manager *manager =
		wl_resource_get_user_data(resource);
	assert(manager);

	wl_list_remove(&manager->link);

	struct wlr_xwayland_keyboard_grab_v1_grab *grab;
	struct wlr_xwayland_keyboard_grab_v1_grab *tmp;

	wl_list_for_each_safe(grab, tmp, &manager->grabs, link) {
		wl_resource_destroy(grab->resource);
	}

	free(manager);
}

static void xwayland_keyboard_grab_manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_xwayland_keyboard_grab_manager_v1_interface keyboard_grab_manager_impl = {
	.destroy = xwayland_keyboard_grab_manager_destroy,
	.grab_keyboard = xwayland_keyboard_grab_grab_keyboard,
};


static void xwayland_keyboard_grab_v1_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	assert(data);
	assert(client);
	struct wlr_xwayland_keyboard_grab_v1 *xwayland_keyboard_grab = data;

	if (client != xwayland_keyboard_grab->xwayland->client) {
		wlr_log(L_DEBUG, "Denying xwayland_keyboard_grab access for client");
		// TODO: Can we post a better error here?
		wl_client_post_no_memory(client);
		return;
	}


	struct wlr_xwayland_keyboard_grab_v1_manager *manager =
		calloc(1, sizeof(struct wlr_xwayland_keyboard_grab_v1_manager));
	if (!manager) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *resource = wl_resource_create(client,
		&zwp_xwayland_keyboard_grab_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		free(manager);
		return;
	}

	manager->xwayland_keyboard_grab = xwayland_keyboard_grab;
	wl_list_insert(&xwayland_keyboard_grab->clients, &manager->link);
	wl_list_init(&manager->grabs);
	wl_resource_set_implementation(resource, &keyboard_grab_manager_impl,
		manager, &xwayland_keyboard_grab_manager_handle_destroy);
	wlr_log(L_DEBUG, "Someting bound to xwayland keyboard grab: %p", manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xwayland_keyboard_grab_v1 *xwayland_keyboard_grab =
		wl_container_of(listener, xwayland_keyboard_grab, display_destroy);

	wlr_xwayland_keyboard_grab_v1_destroy(xwayland_keyboard_grab);
}

struct wlr_xwayland_keyboard_grab_v1 *wlr_xwayland_keyboard_grab_v1_create(
		struct wl_display *display, struct wlr_xwayland *xwayland) {
	assert(display);
	assert(xwayland);
	struct wlr_xwayland_keyboard_grab_v1 *xwayland_keyboard_grab =
		calloc(1, sizeof(struct wlr_xwayland_keyboard_grab_v1));
	if (!xwayland_keyboard_grab) {
		return NULL;
	}

	xwayland_keyboard_grab->global = wl_global_create(display,
		&zwp_xwayland_keyboard_grab_manager_v1_interface, 1,
		xwayland_keyboard_grab, xwayland_keyboard_grab_v1_bind);
	if (!xwayland_keyboard_grab->global) {
		free(xwayland_keyboard_grab);
		return NULL;
	}

	wl_list_init(&xwayland_keyboard_grab->clients);
	wl_signal_init(&xwayland_keyboard_grab->events.new_grab);

	xwayland_keyboard_grab->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &xwayland_keyboard_grab->display_destroy);
	xwayland_keyboard_grab->xwayland = xwayland;

	return xwayland_keyboard_grab;
}

void wlr_xwayland_keyboard_grab_v1_destroy(struct wlr_xwayland_keyboard_grab_v1 *xwayland_keyboard_grab) {
	struct wlr_xwayland_keyboard_grab_v1_manager *manager;
	struct wlr_xwayland_keyboard_grab_v1_manager *tmp;

	wl_list_for_each_safe(manager, tmp, &xwayland_keyboard_grab->clients, link) {
		wl_resource_destroy(manager->resource);
	}

	wl_list_remove(&xwayland_keyboard_grab->display_destroy.link);
	wl_global_destroy(xwayland_keyboard_grab->global);
	free(xwayland_keyboard_grab);
}
