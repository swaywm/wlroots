#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_gtk_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "gtk-primary-selection-protocol.h"
#include "util/signal.h"

#define DEVICE_MANAGER_VERSION 1

static const struct gtk_primary_selection_offer_interface offer_impl;

static struct wlr_gtk_primary_selection_offer *offer_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&gtk_primary_selection_offer_interface, &offer_impl));
	return wl_resource_get_user_data(resource);
}

static void offer_handle_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int32_t fd) {
	struct wlr_gtk_primary_selection_offer *offer =
		offer_from_resource(resource);
	if (offer == NULL) {
		close(fd);
		return;
	}

	offer->source->send(offer->source, mime_type, fd);
}

static void offer_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct gtk_primary_selection_offer_interface offer_impl = {
	.receive = offer_handle_receive,
	.destroy = offer_handle_destroy,
};

static void offer_destroy(struct wlr_gtk_primary_selection_offer *offer) {
	if (offer == NULL) {
		return;
	}
	// Make resource inert
	wl_resource_set_user_data(offer->resource, NULL);
	wl_list_remove(&offer->source_destroy.link);
	free(offer);
}

static void offer_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_gtk_primary_selection_offer *offer =
		offer_from_resource(resource);
	offer_destroy(offer);
}

static void offer_handle_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_gtk_primary_selection_offer *offer =
		wl_container_of(listener, offer, source_destroy);
	offer_destroy(offer);
}

static void offer_create(struct wl_resource *device_resource,
		struct wlr_gtk_primary_selection_source *source) {
	struct wlr_gtk_primary_selection_offer *offer =
		calloc(1, sizeof(struct wlr_gtk_primary_selection_offer));
	if (offer == NULL) {
		wl_resource_post_no_memory(device_resource);
		return;
	}

	struct wl_client *client = wl_resource_get_client(device_resource);
	uint32_t version = wl_resource_get_version(device_resource);
	offer->resource = wl_resource_create(client,
		&gtk_primary_selection_offer_interface, version, 0);
	if (offer->resource == NULL) {
		free(offer);
		wl_resource_post_no_memory(device_resource);
		return;
	}
	wl_resource_set_implementation(offer->resource, &offer_impl, offer,
		offer_handle_resource_destroy);

	offer->source = source;

	offer->source_destroy.notify = offer_handle_source_destroy;
	wl_signal_add(&source->events.destroy, &offer->source_destroy);

	gtk_primary_selection_device_send_data_offer(device_resource,
		offer->resource);

	char **p;
	wl_array_for_each(p, &source->mime_types) {
		gtk_primary_selection_offer_send_offer(offer->resource, *p);
	}

	gtk_primary_selection_device_send_selection(device_resource,
		offer->resource);
}


struct client_data_source {
	struct wlr_gtk_primary_selection_source source;
	struct wl_resource *resource;
};

static void client_source_send(
		struct wlr_gtk_primary_selection_source *wlr_source,
		const char *mime_type, int32_t fd) {
	struct client_data_source *source = (struct client_data_source *)wlr_source;
	gtk_primary_selection_source_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void client_source_cancel(
		struct wlr_gtk_primary_selection_source *wlr_source) {
	struct client_data_source *source = (struct client_data_source *)wlr_source;
	gtk_primary_selection_source_send_cancelled(source->resource);
	// TODO: make the source resource inert
}

static const struct gtk_primary_selection_source_interface source_impl;

static struct client_data_source *client_data_source_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&gtk_primary_selection_source_interface, &source_impl));
	return wl_resource_get_user_data(resource);
}

static void source_handle_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct client_data_source *source =
		client_data_source_from_resource(resource);

	char **p = wl_array_add(&source->source.mime_types, sizeof(*p));
	if (p) {
		*p = strdup(mime_type);
	}
	if (p == NULL || *p == NULL) {
		if (p) {
			source->source.mime_types.size -= sizeof(*p);
		}
		wl_resource_post_no_memory(resource);
	}
}

static void source_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct gtk_primary_selection_source_interface source_impl = {
	.offer = source_handle_offer,
	.destroy = source_handle_destroy,
};

static void source_resource_handle_destroy(struct wl_resource *resource) {
	struct client_data_source *source =
		client_data_source_from_resource(resource);
	wlr_gtk_primary_selection_source_finish(&source->source);
	free(source);
}


static const struct gtk_primary_selection_device_interface device_impl;

static struct wlr_gtk_primary_selection_device *device_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&gtk_primary_selection_device_interface, &device_impl));
	return wl_resource_get_user_data(resource);
}

static void device_handle_set_selection(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *source_resource,
		uint32_t serial) {
	struct wlr_gtk_primary_selection_device *device =
		device_from_resource(resource);
	if (device == NULL) {
		return;
	}

	struct client_data_source *source = NULL;
	if (source_resource != NULL) {
		source = client_data_source_from_resource(source_resource);
	}

	// TODO: improve serial checking
	struct wlr_seat *seat = device->seat;
	if (seat->primary_selection_serial > 0 &&
			seat->primary_selection_serial - serial < UINT32_MAX / 2) {
		return;
	}
	seat->primary_selection_serial = serial;

	wlr_gtk_primary_selection_device_manager_set_selection(device->manager,
		seat, &source->source);
}

static void device_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct gtk_primary_selection_device_interface device_impl = {
	.set_selection = device_handle_set_selection,
	.destroy = device_handle_destroy,
};

static void device_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}


static void device_resource_send_selection(struct wl_resource *resource,
		struct wlr_gtk_primary_selection_source *source) {
	assert(device_from_resource(resource) != NULL);

	if (source != NULL) {
		offer_create(resource, source);
	} else {
		gtk_primary_selection_device_send_selection(resource, NULL);
	}
}

static void device_send_selection(
		struct wlr_gtk_primary_selection_device *device) {
	struct wlr_seat_client *seat_client =
		device->seat->keyboard_state.focused_client;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &device->resources) {
		if (wl_resource_get_client(resource) == seat_client->client) {
			device_resource_send_selection(resource, device->source);
		}
	}
}

static void device_handle_source_destroy(struct wl_listener *listener,
	void *data);

static void device_set_selection(
		struct wlr_gtk_primary_selection_device *device,
		struct wlr_gtk_primary_selection_source *source) {
	if (source != NULL) {
		assert(source->send);
		assert(source->cancel);
	}

	if (device->source != NULL) {
		wl_list_remove(&device->source_destroy.link);
		device->source->cancel(device->source);
		device->source = NULL;
	}

	struct wlr_gtk_primary_selection_offer *offer, *tmp;
	wl_list_for_each_safe(offer, tmp, &device->offers, link) {
		offer_destroy(offer);
	}

	device->source = source;
	if (source != NULL) {
		device->source_destroy.notify = device_handle_source_destroy;
		wl_signal_add(&source->events.destroy, &device->source_destroy);
	}

	device_send_selection(device);

	struct wlr_seat *seat = device->seat;
	// TODO: remove these from wlr_seat
	seat->primary_selection_source = source;
	wlr_signal_emit_safe(&seat->events.primary_selection, seat);
}

static void device_destroy(struct wlr_gtk_primary_selection_device *device);

static void device_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_gtk_primary_selection_device *device =
		wl_container_of(listener, device, seat_destroy);
	device_destroy(device);
}

static void device_handle_seat_focus_change(struct wl_listener *listener,
		void *data) {
	struct wlr_gtk_primary_selection_device *device =
		wl_container_of(listener, device, seat_focus_change);
	device_send_selection(device);
}

static void device_handle_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_gtk_primary_selection_device *device =
		wl_container_of(listener, device, source_destroy);
	wl_list_remove(&device->source_destroy.link);
	device->source = NULL;
	device_send_selection(device);
}

static struct wlr_gtk_primary_selection_device *get_or_create_device(
		struct wlr_gtk_primary_selection_device_manager *manager,
		struct wlr_seat *seat) {
	struct wlr_gtk_primary_selection_device *device;
	wl_list_for_each(device, &manager->devices, link) {
		if (device->seat == seat) {
			return device;
		}
	}

	device = calloc(1, sizeof(struct wlr_gtk_primary_selection_device));
	if (device == NULL) {
		return NULL;
	}
	device->manager = manager;
	device->seat = seat;

	wl_list_init(&device->resources);
	wl_list_insert(&manager->devices, &device->link);

	wl_list_init(&device->offers);

	device->seat_destroy.notify = device_handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &device->seat_destroy);

	device->seat_focus_change.notify = device_handle_seat_focus_change;
	wl_signal_add(&seat->keyboard_state.events.focus_change,
		&device->seat_focus_change);

	return device;
}

static void device_destroy(struct wlr_gtk_primary_selection_device *device) {
	if (device == NULL) {
		return;
	}
	wl_list_remove(&device->link);
	wl_list_remove(&device->seat_destroy.link);
	wl_list_remove(&device->seat_focus_change.link);
	struct wlr_gtk_primary_selection_offer *offer, *offer_tmp;
	wl_list_for_each_safe(offer, offer_tmp, &device->offers, link) {
		offer_destroy(offer);
	}
	struct wl_resource *resource, *resource_tmp;
	wl_resource_for_each_safe(resource, resource_tmp, &device->resources) {
		// Make the resource inert
		wl_resource_set_user_data(resource, NULL);

		struct wl_list *link = wl_resource_get_link(resource);
		wl_list_remove(link);
		wl_list_init(link);
	}
	free(device);
}


static const struct gtk_primary_selection_device_manager_interface
	device_manager_impl;

struct wlr_gtk_primary_selection_device_manager *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&gtk_primary_selection_device_manager_interface, &device_manager_impl));
	return wl_resource_get_user_data(resource);
}

void wlr_gtk_primary_selection_source_init(
		struct wlr_gtk_primary_selection_source *source) {
	wl_array_init(&source->mime_types);
	wl_signal_init(&source->events.destroy);
}

void wlr_gtk_primary_selection_source_finish(
		struct wlr_gtk_primary_selection_source *source) {
	if (source == NULL) {
		return;
	}

	wlr_signal_emit_safe(&source->events.destroy, source);

	char **p;
	wl_array_for_each(p, &source->mime_types) {
		free(*p);
	}
	wl_array_release(&source->mime_types);
}

static void device_manager_handle_create_source(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id) {
	struct client_data_source *source =
		calloc(1, sizeof(struct client_data_source));
	if (source == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wlr_gtk_primary_selection_source_init(&source->source);

	uint32_t version = wl_resource_get_version(manager_resource);
	source->resource = wl_resource_create(client,
		&gtk_primary_selection_source_interface, version, id);
	if (source->resource == NULL) {
		free(source);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(source->resource, &source_impl, source,
		source_resource_handle_destroy);

	source->source.send = client_source_send;
	source->source.cancel = client_source_cancel;
}

void device_manager_handle_get_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	struct wlr_gtk_primary_selection_device_manager *manager =
		manager_from_resource(manager_resource);

	struct wlr_gtk_primary_selection_device *device =
		get_or_create_device(manager, seat_client->seat);
	if (device == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}

	uint32_t version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&gtk_primary_selection_device_interface, version, id);
	if (resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, &device_impl, device,
		device_handle_resource_destroy);
	wl_list_insert(&device->resources, wl_resource_get_link(resource));

	if (device->seat->keyboard_state.focused_client == seat_client) {
		device_resource_send_selection(resource, device->source);
	}
}

static void device_manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct gtk_primary_selection_device_manager_interface
		device_manager_impl = {
	.create_source = device_manager_handle_create_source,
	.get_device = device_manager_handle_get_device,
	.destroy = device_manager_handle_destroy,
};

static void device_manager_handle_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}


void wlr_gtk_primary_selection_device_manager_set_selection(
		struct wlr_gtk_primary_selection_device_manager *manager,
		struct wlr_seat *seat,
		struct wlr_gtk_primary_selection_source *source) {
	struct wlr_gtk_primary_selection_device *device =
		get_or_create_device(manager, seat);
	if (device == NULL) {
		return;
	}
	device_set_selection(device, source);
}

static void primary_selection_device_manager_bind(struct wl_client *client,
		void *data, uint32_t version, uint32_t id) {
	struct wlr_gtk_primary_selection_device_manager *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&gtk_primary_selection_device_manager_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &device_manager_impl, manager,
		device_manager_handle_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_gtk_primary_selection_device_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_gtk_primary_selection_device_manager_destroy(manager);
}

struct wlr_gtk_primary_selection_device_manager *
		wlr_gtk_primary_selection_device_manager_create(
		struct wl_display *display) {
	struct wlr_gtk_primary_selection_device_manager *manager =
		calloc(1, sizeof(struct wlr_gtk_primary_selection_device_manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->global = wl_global_create(display,
		&gtk_primary_selection_device_manager_interface, DEVICE_MANAGER_VERSION,
		manager, primary_selection_device_manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	wl_list_init(&manager->resources);
	wl_list_init(&manager->devices);
	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

void wlr_gtk_primary_selection_device_manager_destroy(
		struct wlr_gtk_primary_selection_device_manager *manager) {
	if (manager == NULL) {
		return;
	}
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	struct wl_resource *resource, *resource_tmp;
	wl_resource_for_each_safe(resource, resource_tmp, &manager->resources) {
		wl_resource_destroy(resource);
	}
	wl_global_destroy(manager->global);
	free(manager);
}
