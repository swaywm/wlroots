#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_data_device.h>

static void data_device_start_drag(struct wl_client *client, struct wl_resource
		*resource, struct wl_resource *source_resource,
		struct wl_resource *origin, struct wl_resource *icon, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO: data device start drag");
}

static struct wlr_data_offer *wlr_data_source_send_offer(
		struct wlr_data_source *source,
		struct wl_resource *data_device_resourec) {
	// TODO
	return NULL;
}


static void wlr_seat_handle_send_selection(struct wlr_seat_handle *handle) {
	if (!handle->data_device) {
		return;
	}

	if (handle->wlr_seat->selection_source) {
		struct wlr_data_offer *offer =
			wlr_data_source_send_offer(handle->wlr_seat->selection_source,
				handle->data_device);
		wl_data_device_send_selection(handle->data_device, offer->resource);
	} else {
		wl_data_device_send_selection(handle->data_device, NULL);
	}
}

static void wlr_seat_set_selection(struct wlr_seat *seat,
		struct wlr_data_source *source, uint32_t serial) {
	if (seat->selection_source &&
			seat->selection_serial - serial < UINT32_MAX / 2) {
		return;
	}

	if (seat->selection_source) {
		// TODO cancel
	}

	seat->selection_source = source;
	seat->selection_serial = serial;

	struct wlr_seat_handle *focused_handle =
		seat->keyboard_state.focused_handle;

	if (focused_handle) {
		// TODO send selection to keyboard
		wlr_seat_handle_send_selection(focused_handle);
	}

	// TODO emit selection signal

	if (source) {
		// TODO set destroy listener
	}
}

static void data_device_set_selection(struct wl_client *client,
		struct wl_resource *seat_resource, struct wl_resource *source_resource,
		uint32_t serial) {
	if (!source_resource) {
		return;
	}

	struct wlr_data_source *source = wl_resource_get_user_data(source_resource);
	struct wlr_seat_handle *handle = wl_resource_get_user_data(seat_resource);

	// TODO: store serial and check against incoming serial here
	wlr_seat_set_selection(handle->wlr_seat, source, serial);
}

static void data_device_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_data_device_interface data_device_impl = {
	.start_drag = data_device_start_drag,
	.set_selection = data_device_set_selection,
	.release = data_device_release,
};

void data_device_manager_get_data_device(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *seat_resource) {
	struct wlr_seat_handle *handle = wl_resource_get_user_data(seat_resource);

	struct wl_resource *resource =
		wl_resource_create(client,
			&wl_data_device_interface,
			wl_resource_get_version(manager_resource), id);
	if (resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}

	// TODO handle a seat handle having multiple data devices
	assert(handle->data_device == NULL);
	handle->data_device = resource;

	wl_resource_set_implementation(resource, &data_device_impl,
		handle, NULL);
}

static void data_source_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_source *source =
		wl_resource_get_user_data(resource);
	char **p;

	wl_signal_emit(&source->events.destroy, source);

	wl_array_for_each(p, &source->mime_types) {
		free(*p);
	}

	wl_array_release(&source->mime_types);

	free(source);
}

static void data_source_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions) {
	wlr_log(L_DEBUG, "TODO: data source set actions");
}

static void data_source_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct wlr_data_source *source =
		wl_resource_get_user_data(resource);
	char **p;

	p = wl_array_add(&source->mime_types, sizeof *p);

	if (p) {
		*p = strdup(mime_type);
	}
	if (!p || !*p){
		wl_resource_post_no_memory(resource);
	}
}

static struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void data_device_manager_create_data_source(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	struct wlr_data_source *source = calloc(1, sizeof(struct wlr_data_source));
	if (source == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	source->resource =
		wl_resource_create(client, &wl_data_source_interface,
			wl_resource_get_version(resource), id);
	if (source->resource == NULL) {
		free(source);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_array_init(&source->mime_types);
	wl_signal_init(&source->events.destroy);

	wl_resource_set_implementation(source->resource, &data_source_impl,
		source, data_source_resource_destroy);
}

static const struct wl_data_device_manager_interface
data_device_manager_impl = {
	.create_data_source = data_device_manager_create_data_source,
	.get_data_device = data_device_manager_get_data_device,
};

static void data_device_manager_bind(struct wl_client *client,
		void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource;

	resource = wl_resource_create(client,
			&wl_data_device_manager_interface,
			version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &data_device_manager_impl,
		NULL, NULL);
}

struct wlr_data_device_manager *wlr_data_device_manager_create(
		struct wl_display *display) {
	struct wlr_data_device_manager *manager =
		calloc(1, sizeof(struct wlr_data_device_manager));
	if (manager == NULL) {
		wlr_log(L_ERROR, "could not create data device manager");
		return NULL;
	}

	manager->global =
		wl_global_create(display, &wl_data_device_manager_interface,
			3, NULL, data_device_manager_bind);

	if (!manager->global) {
		wlr_log(L_ERROR, "could not create data device manager wl global");
		free(manager);
		return NULL;
	}

	return manager;
}
