#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_passive_grab.h>

#include <xwayland-keyboard-grab-unstable-v1-protocol.h>

struct wlr_passive_grab_manager {
	struct wl_resource *resource;
	struct wlr_passive_grab_v1 *passive_grab;

	struct wl_list link; // wlr_passive_grab_v1::clients;
	struct wl_list grabs;

	struct {
		struct wl_signal destroy;
	} events;
};

static void passive_grab_grab_handle_destroy(struct wl_resource *resource) {
	assert(resource);
	struct wlr_passive_grab_grab *grab = wl_resource_get_user_data(resource);
	assert(grab);

	wl_signal_emit(&grab->events.destroy, grab);
	wl_list_remove(&grab->link);
	free(grab);
}

static void passive_grab_grab_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_xwayland_keyboard_grab_v1_interface passive_grab_impl = {
	.destroy = passive_grab_grab_destroy,
};

static void passive_grab_grab_keyboard(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface, struct wl_resource *seat) {
	assert(client);
	assert(resource);
	assert(surface);
	assert(seat);

	struct wlr_passive_grab_grab *grab = calloc(1, sizeof(struct wlr_passive_grab_grab));
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

	grab->seat = wl_resource_get_user_data(seat);
	assert(grab->seat);
	grab->surface = wl_resource_get_user_data(surface);
	assert(grab->surface);

	wl_resource_set_implementation(resource, &passive_grab_impl,
		grab, &passive_grab_grab_handle_destroy);

	struct wlr_passive_grab_manager *manager = wl_resource_get_user_data(resource);
	assert(manager);

	wl_list_insert(&grab->link, &manager->grabs);
	wlr_log(L_DEBUG, "Someting aquired an xwayland keyboard grab");
	wl_signal_emit(&manager->passive_grab->events.new_grab, grab);
}

static void passive_grab_manager_handle_destroy(struct wl_resource *resource) {
	assert(resource);
	struct wlr_passive_grab_manager *manager = wl_resource_get_user_data(resource);
	assert(manager);

	wl_list_remove(&manager->link);

	struct wlr_passive_grab_grab *grab;
	struct wlr_passive_grab_grab *tmp;

	wl_list_for_each_safe(grab, tmp, &manager->grabs, link) {
		wl_resource_destroy(grab->resource);
	}

	free(manager);
}

static void passive_grab_manager_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_xwayland_keyboard_grab_manager_v1_interface keyboard_grab_manager_impl = {
	.destroy = passive_grab_manager_destroy,
	.grab_keyboard = passive_grab_grab_keyboard,
};


static void passive_grab_bind_v1(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	assert(data);
	struct wlr_passive_grab_v1 *passive_grab = data;
	struct wlr_passive_grab_manager *manager = calloc(1, sizeof(struct wlr_passive_grab_manager));
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

	wl_list_insert(&passive_grab->clients, &manager->link);
	wl_list_init(&manager->grabs);
	wl_resource_set_implementation(resource, &keyboard_grab_manager_impl,
		manager, &passive_grab_manager_handle_destroy);
	wlr_log(L_DEBUG, "Someting bound to xwayland keyboard grab");
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_passive_grab_v1 *passive_grab = wl_container_of(listener,
		passive_grab, display_destroy);

	wlr_passive_grab_destroy_v1(passive_grab);
}

struct wlr_passive_grab_v1 *wlr_passive_grab_create_v1(struct wl_display *display) {
	assert(display);
	struct wlr_passive_grab_v1 *passive_grab = calloc(1, sizeof(struct wlr_passive_grab_v1));
	if (!passive_grab) {
		return NULL;
	}

	passive_grab->global = wl_global_create(display,
		&zwp_xwayland_keyboard_grab_manager_v1_interface, 1, passive_grab,
		passive_grab_bind_v1);
	wl_list_init(&passive_grab->clients);

	passive_grab->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &passive_grab->display_destroy);

	return passive_grab;
}

void wlr_passive_grab_destroy_v1(struct wlr_passive_grab_v1 *passive_grab) {
	struct wlr_passive_grab_manager *manager;
	struct wlr_passive_grab_manager *tmp;

	wl_list_for_each_safe(manager, tmp, &passive_grab->clients, link) {
		wl_resource_destroy(manager->resource);
	}

	wl_list_remove(&passive_grab->display_destroy.link);
	wl_global_destroy(passive_grab->global);
	free(passive_grab);
}
