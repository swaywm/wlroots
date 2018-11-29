#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include "util/signal.h"
#include "wlr-data-control-unstable-v1-protocol.h"

#define DATA_CONTROL_MANAGER_VERSION 1

struct client_data_source {
	struct wlr_data_source source;
	struct wl_resource *resource;
	bool finalized;
};

static const struct wlr_data_source_impl client_source_impl;

static struct client_data_source *
		client_data_source_from_source(struct wlr_data_source *wlr_source) {
	assert(wlr_source->impl == &client_source_impl);
	return (struct client_data_source *)wlr_source;
}

static void client_source_send(struct wlr_data_source *wlr_source,
		const char *mime_type, int fd) {
	struct client_data_source *source =
		client_data_source_from_source(wlr_source);
	zwlr_data_control_source_v1_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void client_source_cancel(struct wlr_data_source *wlr_source) {
	struct client_data_source *source =
		client_data_source_from_source(wlr_source);
	zwlr_data_control_source_v1_send_cancelled(source->resource);
	wlr_data_source_finish(wlr_source);
	// Make the resource inert
	wl_resource_set_user_data(source->resource, NULL);
	free(source);
}

static const struct wlr_data_source_impl client_source_impl = {
	.send = client_source_send,
	.cancel = client_source_cancel,
};

static const struct zwlr_data_control_source_v1_interface source_impl;

static struct client_data_source *source_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_data_control_source_v1_interface, &source_impl));
	return wl_resource_get_user_data(resource);
}

static void source_handle_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct client_data_source *source = source_from_resource(resource);
	if (source == NULL) {
		return;
	}

	if (source->finalized) {
		wl_resource_post_error(resource,
			ZWLR_DATA_CONTROL_SOURCE_V1_ERROR_INVALID_OFFER,
			"cannot mutate offer after set_selection");
		return;
	}

	char *dup_mime_type = strdup(mime_type);
	if (dup_mime_type == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	char **p = wl_array_add(&source->source.mime_types, sizeof(char *));
	if (p == NULL) {
		free(dup_mime_type);
		wl_resource_post_no_memory(resource);
		return;
	}

	*p = dup_mime_type;
}

static void source_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_data_control_source_v1_interface source_impl = {
	.offer = source_handle_offer,
	.destroy = source_handle_destroy,
};

static void source_handle_resource_destroy(struct wl_resource *resource) {
	struct client_data_source *source = source_from_resource(resource);
	if (source == NULL) {
		return;
	}
	wlr_data_source_cancel(&source->source);
}


static const struct zwlr_data_control_offer_v1_interface offer_impl;

static struct wlr_data_control_device_v1 *control_from_offer_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_data_control_offer_v1_interface, &offer_impl));
	return wl_resource_get_user_data(resource);
}

static void offer_handle_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int fd) {
	struct wlr_data_control_device_v1 *device = control_from_offer_resource(resource);
	if (device == NULL || device->seat->selection_source == NULL) {
		close(fd);
		return;
	}
	wlr_data_source_send(device->seat->selection_source, mime_type, fd);
}

static void offer_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_data_control_offer_v1_interface offer_impl = {
	.receive = offer_handle_receive,
	.destroy = offer_handle_destroy,
};

static void offer_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_control_device_v1 *device = control_from_offer_resource(resource);
	if (device != NULL) {
		device->selection_offer_resource = NULL;
	}
}

static struct wl_resource *create_offer(struct wlr_data_control_device_v1 *device,
		struct wlr_data_source *source) {
	struct wl_client *client = wl_resource_get_client(device->resource);
	uint32_t version = wl_resource_get_version(device->resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_data_control_offer_v1_interface, version, 0);
	if (resource == NULL) {
		return NULL;
	}
	wl_resource_set_implementation(resource, &offer_impl, device,
		offer_handle_resource_destroy);

	zwlr_data_control_device_v1_send_data_offer(device->resource, resource);

	char **p;
	wl_array_for_each(p, &source->mime_types) {
		zwlr_data_control_offer_v1_send_offer(resource, *p);
	}

	return resource;
}


static const struct zwlr_data_control_device_v1_interface control_impl;

static struct wlr_data_control_device_v1 *control_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_data_control_device_v1_interface, &control_impl));
	return wl_resource_get_user_data(resource);
}

static void control_handle_set_selection(struct wl_client *client,
		struct wl_resource *control_resource,
		struct wl_resource *source_resource) {
	struct wlr_data_control_device_v1 *device =
		control_from_resource(control_resource);
	struct client_data_source *source = source_from_resource(source_resource);
	if (device == NULL) {
		return;
	}

	struct wlr_data_source *wlr_source = source ? &source->source : NULL;
	wlr_seat_request_set_selection(device->seat, wlr_source,
		wl_display_next_serial(device->seat->display));
}

static void control_handle_destroy(struct wl_client *client,
		struct wl_resource *control_resource) {
	wl_resource_destroy(control_resource);
}

static const struct zwlr_data_control_device_v1_interface control_impl = {
	.set_selection = control_handle_set_selection,
	.destroy = control_handle_destroy,
};

static void control_send_selection(struct wlr_data_control_device_v1 *device) {
	struct wlr_data_source *source = device->seat->selection_source;

	if (device->selection_offer_resource != NULL) {
		// Make the offer inert
		wl_resource_set_user_data(device->selection_offer_resource, NULL);
	}

	device->selection_offer_resource = NULL;
	if (source != NULL) {
		device->selection_offer_resource = create_offer(device, source);
		if (device->selection_offer_resource == NULL) {
			wl_resource_post_no_memory(device->resource);
			return;
		}
	}

	zwlr_data_control_device_v1_send_selection(device->resource,
		device->selection_offer_resource);
}

static void control_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_control_device_v1 *device = control_from_resource(resource);
	wlr_data_control_device_v1_destroy(device);
}

static void control_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_data_control_device_v1 *device =
		wl_container_of(listener, device, seat_destroy);
	wlr_data_control_device_v1_destroy(device);
}

static void control_handle_seat_set_selection(struct wl_listener *listener,
		void *data) {
	struct wlr_data_control_device_v1 *device =
		wl_container_of(listener, device, seat_set_selection);
	control_send_selection(device);
}

void wlr_data_control_device_v1_destroy(struct wlr_data_control_device_v1 *device) {
	if (device == NULL) {
		return;
	}
	zwlr_data_control_device_v1_send_finished(device->resource);
	// Make the resources inert
	wl_resource_set_user_data(device->resource, NULL);
	if (device->selection_offer_resource != NULL) {
		wl_resource_set_user_data(device->selection_offer_resource, NULL);
	}
	wl_list_remove(&device->seat_destroy.link);
	wl_list_remove(&device->seat_set_selection.link);
	wl_list_remove(&device->link);
	free(device);
}


static const struct zwlr_data_control_manager_v1_interface manager_impl;

static struct wlr_data_control_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_data_control_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void manager_handle_create_data_source(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id) {
	struct client_data_source *source =
		calloc(1, sizeof(struct client_data_source));
	if (source == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wlr_data_source_init(&source->source, &client_source_impl);

	uint32_t version = wl_resource_get_version(manager_resource);
	source->resource = wl_resource_create(client,
		&zwlr_data_control_source_v1_interface, version, id);
	if (source->resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		free(source);
		return;
	}
	wl_resource_set_implementation(source->resource, &source_impl, source,
		source_handle_resource_destroy);
}

static void manager_handle_get_data_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_data_control_manager_v1 *manager =
		manager_from_resource(manager_resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);

	struct wlr_data_control_device_v1 *device =
		calloc(1, sizeof(struct wlr_data_control_device_v1));
	if (device == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	device->manager = manager;
	device->seat = seat_client->seat;

	uint32_t version = wl_resource_get_version(manager_resource);
	device->resource = wl_resource_create(client,
		&zwlr_data_control_device_v1_interface, version, id);
	if (device->resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		free(device);
		return;
	}
	wl_resource_set_implementation(device->resource, &control_impl, device,
		control_handle_resource_destroy);
	struct wl_resource *resource = device->resource;

	device->seat_destroy.notify = control_handle_seat_destroy;
	wl_signal_add(&device->seat->events.destroy, &device->seat_destroy);

	device->seat_set_selection.notify = control_handle_seat_set_selection;
	wl_signal_add(&device->seat->events.set_selection,
		&device->seat_set_selection);

	wl_list_insert(&manager->devices, &device->link);
	wlr_signal_emit_safe(&manager->events.new_device, device);

	// At this point maybe the compositor decided to destroy the device. If
	// it's the case then the resource will be inert.
	device = control_from_resource(resource);
	if (device != NULL) {
		control_send_selection(device);
	}
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwlr_data_control_manager_v1_interface manager_impl = {
	.create_data_source = manager_handle_create_data_source,
	.get_data_device = manager_handle_get_data_device,
	.destroy = manager_handle_destroy,
};

static void manager_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	struct wlr_data_control_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_data_control_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager,
		manager_handle_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_data_control_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_data_control_manager_v1_destroy(manager);
}

struct wlr_data_control_manager_v1 *wlr_data_control_manager_v1_create(
		struct wl_display *display) {
	struct wlr_data_control_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_data_control_manager_v1));
	if (manager == NULL) {
		return NULL;
	}
	wl_list_init(&manager->resources);
	wl_list_init(&manager->devices);
	wl_signal_init(&manager->events.destroy);
	wl_signal_init(&manager->events.new_device);

	manager->global = wl_global_create(display,
		&zwlr_data_control_manager_v1_interface, DATA_CONTROL_MANAGER_VERSION,
		manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

void wlr_data_control_manager_v1_destroy(
		struct wlr_data_control_manager_v1 *manager) {
	if (manager == NULL) {
		return;
	}

	wlr_signal_emit_safe(&manager->events.destroy, manager);

	struct wlr_data_control_device_v1 *device, *control_tmp;
	wl_list_for_each_safe(device, control_tmp, &manager->devices, link) {
		wl_resource_destroy(device->resource);
	}

	struct wl_resource *resource, *resource_tmp;
	wl_resource_for_each_safe(resource, resource_tmp, &manager->resources) {
		wl_resource_destroy(resource);
	}

	wl_list_remove(&manager->display_destroy.link);
	free(manager);
}
