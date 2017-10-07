#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_data_device_manager.h>
#include <wlr/types/wlr_data_source.h>
#include <wlr/types/wlr_seat.h>

static void resource_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_device_start_drag(struct wl_client *client,
		struct wl_resource *res, struct wl_resource *source_resource,
		struct wl_resource *origin_res, struct wl_resource *icon_res,
		uint32_t serial) {
	wlr_log(L_DEBUG, "TODO: implement data_device:start_drag");

	// Will probably look like this:
	// struct wlr_seat_handle *handle = wl_resource_get_user_data(res);
	// struct wlr_data_device *device = handle->wlr_seat->data_device;
	// struct wlr_data_source *src = wl_resource_get_user_data(src_res);
	// struct wlr_surface *origin = wl_resource_get_user_data(origin_res);
	// struct wlr_surface *icon;
	// if (icon_res)
	// 	icon = wl_resource_get_user_data(icon_res);
	// wlr_seat_start_drag(serial, device->seat, src,
	// 	origin, icon); // will set surface roles and emit signal for user
}

static void data_device_set_selection(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *source_resource,
		uint32_t serial) {
	if (!source_resource) {
		return;
	}

	// TODO: serial validation
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	struct wlr_data_device *device = handle->wlr_seat->data_device;
	struct wlr_data_source *source = wl_resource_get_user_data(source_resource);
	wlr_data_device_set_selection(device, source);
}

static struct wl_data_device_interface data_device_impl = {
	.start_drag = data_device_start_drag,
	.set_selection = data_device_set_selection,
	.release = resource_destroy
};

static void data_device_selection_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_data_device *device =
		wl_container_of(listener, device, selection_destroyed);
	assert(data == device->selection);
	device->selection = NULL; // make sure no cancel is sent
	wlr_data_device_set_selection(device, NULL);
}

static struct wlr_data_device *seat_ensure_data_device(
		struct wlr_data_device_manager *manager, struct wlr_seat *seat) {
	if (seat->data_device) {
		return seat->data_device;
	}

	seat->data_device = calloc(1, sizeof(*seat->data_device));
	if (!seat->data_device) {
		wlr_log(L_ERROR, "Failed to allocate wlr_data_device");
		return NULL;
	}

	seat->data_device->seat = seat;
	wl_signal_init(&seat->data_device->events.selection_change);
	seat->data_device->selection_destroyed.notify =
		data_device_selection_destroy;
	return seat->data_device;
}

static void data_device_destroy(struct wl_resource *resource) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(resource);
	handle->data_device = NULL;
}

static void data_device_manager_create_data_source(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	uint32_t version = wl_resource_get_version(resource);
	if (!wlr_wl_data_source_create(client, version, id)) {
		wlr_log(L_ERROR, "Failed to create wlr_wl_data_source");
		wl_resource_post_no_memory(resource);
		return;
	}
}

static void data_device_manager_get_data_device(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_data_device_manager *manager =
		wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);
	struct wlr_data_device *device;
	if (!(device = seat_ensure_data_device(manager, seat_handle->wlr_seat))) {
		wl_resource_post_no_memory(resource);
		return;
	}

	if (seat_handle->data_device) {
		// TODO: implement resource lists for seat related handles
		//   this is a protocol violation, see the todos in wlr_seat.c
		wl_resource_destroy(seat_handle->data_device);
	}

	seat_handle->data_device = wl_resource_create(client,
		&wl_data_device_interface, wl_resource_get_version(resource), id);
	if (!seat_handle->data_device) {
		wlr_log(L_ERROR, "Failed to create wl_data_device resource");
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(seat_handle->data_device, &data_device_impl,
		seat_handle, data_device_destroy);
}

struct wl_data_device_manager_interface data_device_manager_impl = {
	.create_data_source = data_device_manager_create_data_source,
	.get_data_device = data_device_manager_get_data_device
};

static void data_device_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_data_device_manager *manager = data;
	assert(client && manager);
	if (version > 3) {
		wlr_log(L_ERROR, "Client requested unsupported data_device_manager "
			"version, disconnecting");
		wl_client_destroy(client);
		return;
	}
	struct wl_resource *resource = wl_resource_create(
			client, &wl_data_device_manager_interface, version, id);
	if (!resource) {
		wlr_log(L_ERROR, "Failed to allocate wl_data_device_manager resource");
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &data_device_manager_impl,
		manager, NULL);
}

struct wlr_data_device_manager *wlr_data_device_manager_create(
		struct wl_display *display) {
	struct wlr_data_device_manager *manager = calloc(1, sizeof(*manager));
	if (!manager) {
		wlr_log(L_ERROR, "Failed to allocated wlr_data_device_manager");
		return NULL;
	}

	manager->global =
		wl_global_create(display, &wl_data_device_manager_interface, 3, manager,
			data_device_manager_bind);

	if (!manager->global) {
		wlr_log(L_ERROR, "Failed to create global for wlr_data_device_manager");
		free(manager);
		return NULL;
	}

	return manager;
}

void wlr_data_device_manager_destroy(struct wlr_data_device_manager *manager) {
	if (manager) {
		wl_global_destroy(manager->global);
		free(manager);
	}
}

void wlr_data_device_set_selection(struct wlr_data_device *device,
		struct wlr_data_source *source) {
	if (device->selection == source) {
		return;
	}

	if (device->selection) {
		wl_list_remove(&device->selection_destroyed.link);
		wlr_data_source_cancelled(device->selection);
	}

	device->selection = source;
	wl_signal_emit(&device->events.selection_change, device);

	if (source) {
		wl_signal_add(&source->events.destroy, &device->selection_destroyed);
	}
}
