#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gtk-primary-selection-protocol.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

static void client_source_send(struct wlr_primary_selection_source *source,
		const char *mime_type, int32_t fd) {
	gtk_primary_selection_source_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void client_source_cancel(
		struct wlr_primary_selection_source *source) {
	gtk_primary_selection_source_send_cancelled(source->resource);
}


static void offer_handle_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int32_t fd) {
	struct wlr_primary_selection_offer *offer =
		wl_resource_get_user_data(resource);

	if (offer->source && offer == offer->source->offer) {
		offer->source->send(offer->source, mime_type, fd);
	} else {
		close(fd);
	}
}

static void offer_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct gtk_primary_selection_offer_interface offer_impl = {
	.receive = offer_handle_receive,
	.destroy = offer_handle_destroy,
};

static void offer_resource_handle_destroy(struct wl_resource *resource) {
	struct wlr_primary_selection_offer *offer =
		wl_resource_get_user_data(resource);

	if (!offer->source) {
		goto out;
	}

	wl_list_remove(&offer->source_destroy.link);

	if (offer->source->offer != offer) {
		goto out;
	}

	if (offer->source->resource) {
		gtk_primary_selection_source_send_cancelled(offer->source->resource);
	}

	offer->source->offer = NULL;
out:
	free(offer);
}

static void offer_handle_source_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_primary_selection_offer *offer =
		wl_container_of(listener, offer, source_destroy);

	offer->source = NULL;
}


static struct wlr_primary_selection_offer *source_send_offer(
		struct wlr_primary_selection_source *source,
		struct wlr_seat_client *target) {
	if (wl_list_empty(&target->primary_selection_devices)) {
		return NULL;
	}

	struct wlr_primary_selection_offer *offer =
		calloc(1, sizeof(struct wlr_primary_selection_offer));
	if (offer == NULL) {
		return NULL;
	}

	uint32_t version = wl_resource_get_version(
		wl_resource_from_link(target->primary_selection_devices.next));
	offer->resource = wl_resource_create(target->client,
		&gtk_primary_selection_offer_interface, version, 0);
	if (offer->resource == NULL) {
		free(offer);
		return NULL;
	}
	wl_resource_set_implementation(offer->resource, &offer_impl, offer,
		offer_resource_handle_destroy);

	offer->source_destroy.notify = offer_handle_source_destroy;
	wl_signal_add(&source->events.destroy, &offer->source_destroy);

	struct wl_resource *target_resource;
	wl_resource_for_each(target_resource, &target->primary_selection_devices) {
		gtk_primary_selection_device_send_data_offer(target_resource,
			offer->resource);
	}

	char **p;
	wl_array_for_each(p, &source->mime_types) {
		gtk_primary_selection_offer_send_offer(offer->resource, *p);
	}

	offer->source = source;
	source->offer = offer;

	return offer;
}

static void source_handle_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct wlr_primary_selection_source *source =
		wl_resource_get_user_data(resource);

	char **p = wl_array_add(&source->mime_types, sizeof(*p));
	if (p) {
		*p = strdup(mime_type);
	}
	if (p == NULL || *p == NULL) {
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

static void source_resource_handle_destroy( struct wl_resource *resource) {
	struct wlr_primary_selection_source *source =
		wl_resource_get_user_data(resource);
	wl_signal_emit(&source->events.destroy, source);
	free(source);
}


void wlr_seat_client_send_primary_selection(
		struct wlr_seat_client *seat_client) {
	if (wl_list_empty(&seat_client->primary_selection_devices)) {
		return;
	}

	if (seat_client->seat->primary_selection_source) {
		struct wlr_primary_selection_offer *offer = source_send_offer(
			seat_client->seat->primary_selection_source, seat_client);
		if (offer == NULL) {
			return;
		}

		struct wl_resource *resource;
		wl_resource_for_each(resource, &seat_client->primary_selection_devices) {
			gtk_primary_selection_device_send_selection(resource, offer->resource);
		}
	} else {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &seat_client->primary_selection_devices) {
			gtk_primary_selection_device_send_selection(resource, NULL);
		}
	}
}

static void seat_client_primary_selection_source_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, primary_selection_source_destroy);
	struct wlr_seat_client *seat_client = seat->keyboard_state.focused_client;

	if (seat_client && seat->keyboard_state.focused_surface) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &seat_client->primary_selection_devices) {
			gtk_primary_selection_device_send_selection(resource, NULL);
		}
	}

	seat->primary_selection_source = NULL;

	wl_signal_emit(&seat->events.primary_selection, seat);
}

void wlr_seat_set_primary_selection(struct wlr_seat *seat,
		struct wlr_primary_selection_source *source, uint32_t serial) {
	if (seat->primary_selection_source &&
			seat->primary_selection_serial - serial < UINT32_MAX / 2) {
		return;
	}

	if (seat->primary_selection_source) {
		seat->primary_selection_source->cancel(seat->primary_selection_source);
		seat->primary_selection_source = NULL;
		wl_list_remove(&seat->primary_selection_source_destroy.link);
	}

	seat->primary_selection_source = source;
	seat->primary_selection_serial = serial;

	struct wlr_seat_client *focused_client =
		seat->keyboard_state.focused_client;
	if (focused_client) {
		wlr_seat_client_send_primary_selection(focused_client);
	}

	wl_signal_emit(&seat->events.primary_selection, seat);

	if (source) {
		seat->primary_selection_source_destroy.notify =
			seat_client_primary_selection_source_destroy;
		wl_signal_add(&source->events.destroy,
			&seat->primary_selection_source_destroy);
	}
}

static void device_handle_set_selection(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *source_resource,
		uint32_t serial) {
	struct wlr_primary_selection_source *source = NULL;
	if (source_resource != NULL) {
		source = wl_resource_get_user_data(source_resource);
	}

	struct wlr_seat_client *seat_client =
		wl_resource_get_user_data(resource);

	// TODO: store serial and check against incoming serial here
	wlr_seat_set_primary_selection(seat_client->seat, source, serial);
}

static void device_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct gtk_primary_selection_device_interface device_impl = {
	.set_selection = device_handle_set_selection,
	.destroy = device_handle_destroy,
};

static void device_resource_handle_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}


static void device_manager_handle_create_source(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id) {
	struct wlr_primary_selection_source *source =
		calloc(1, sizeof(struct wlr_primary_selection_source));
	if (source == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	int version = wl_resource_get_version(manager_resource);
	source->resource = wl_resource_create(client,
		&gtk_primary_selection_source_interface, version, id);
	if (source->resource == NULL) {
		free(source);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(source->resource, &source_impl, source,
		source_resource_handle_destroy);

	source->send = client_source_send;
	source->cancel = client_source_cancel;

	wl_array_init(&source->mime_types);
	wl_signal_init(&source->events.destroy);
}

void device_manager_handle_get_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_seat_client *seat_client =
		wl_resource_get_user_data(seat_resource);

	uint32_t version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&gtk_primary_selection_device_interface, version, id);
	if (resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, &device_impl, seat_client,
		&device_resource_handle_destroy);
	wl_list_insert(&seat_client->primary_selection_devices,
		wl_resource_get_link(resource));
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


static void primary_selection_device_manager_bind(struct wl_client *client,
		void *data, uint32_t version, uint32_t id) {
	struct wlr_primary_selection_device_manager *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&gtk_primary_selection_device_manager_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &device_manager_impl, manager,
		NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_primary_selection_device_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_primary_selection_device_manager_destroy(manager);
}

struct wlr_primary_selection_device_manager *
		wlr_primary_selection_device_manager_create(
		struct wl_display *display) {
	struct wlr_primary_selection_device_manager *manager =
		calloc(1, sizeof(struct wlr_primary_selection_device_manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->global = wl_global_create(display,
		&gtk_primary_selection_device_manager_interface, 1, manager,
		primary_selection_device_manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

void wlr_primary_selection_device_manager_destroy(
		struct wlr_primary_selection_device_manager *manager) {
	if (manager == NULL) {
		return;
	}
	// TODO: free wl_resources
	wl_global_destroy(manager->global);
	free(manager);
}
