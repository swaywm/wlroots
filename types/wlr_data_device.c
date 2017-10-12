#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_data_device.h>

static void data_device_start_drag(struct wl_client *client, struct wl_resource
		*resource, struct wl_resource *source_resource,
		struct wl_resource *origin, struct wl_resource *icon, uint32_t serial) {
	wlr_log(L_DEBUG, "TODO: data device start drag");
}
static void data_source_accept(struct wlr_data_source *source,
		uint32_t time, const char *mime_type) {
	wl_data_source_send_target(source->resource, mime_type);
}

static void data_source_send(struct wlr_data_source *source,
		const char *mime_type, int32_t fd) {
	wl_data_source_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void data_source_cancel(struct wlr_data_source *source) {
	wl_data_source_send_cancelled(source->resource);
}


static void data_offer_accept(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial, const char *mime_type) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (!offer->source || offer != offer->source->offer) {
		return;
	}

	// TODO check that client is currently focused by the input device

	data_source_accept(offer->source, serial, mime_type);
	offer->source->accepted = (mime_type != NULL);
}

static void data_offer_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int32_t fd) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (offer->source && offer == offer->source->offer) {
		data_source_send(offer->source, mime_type, fd);
	} else {
		close(fd);
	}
}
static void data_offer_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_notify_finish(struct wlr_data_source *source) {
	// TODO
	/*
	if (!source->actions_set) {
		return;
	}

	if (source->offer->in_ask &&
			wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
		wl_data_source_send_action(source->resource,
				source->current_dnd_action);
	}

	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
		wl_data_source_send_dnd_finished(source->resource);
	}
	*/

	source->offer = NULL;
}

static void data_offer_finish(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (!offer->source || offer->source->offer != offer) {
		return;
	}

	data_source_notify_finish(offer->source);
}

static void data_offer_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions,
		uint32_t preferred_action) {
	// TODO
}

static void data_offer_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_offer *offer = wl_resource_get_user_data(resource);

	if (!offer->source) {
		goto out;
	}

	wl_list_remove(&offer->source_destroy.link);

	if (offer->source->offer != offer) {
		goto out;
	}

	// If the drag destination has version < 3, wl_data_offer.finish
	// won't be called, so do this here as a safety net, because
	// we still want the version >=3 drag source to be happy.
	if (wl_resource_get_version(offer->resource) <
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		data_source_notify_finish(offer->source);
	} else if (offer->source->resource &&
			wl_resource_get_version(offer->source->resource) >=
			WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
		wl_data_source_send_cancelled(offer->source->resource);
	}

	offer->source->offer = NULL;
out:
	free(offer);
}

static const struct wl_data_offer_interface data_offer_impl = {
	.accept = data_offer_accept,
	.receive = data_offer_receive,
	.destroy = data_offer_destroy,
	.finish = data_offer_finish,
	.set_actions = data_offer_set_actions,
};

static void handle_offer_source_destroyed(struct wl_listener *listener,
		void *data) {
	struct wlr_data_offer *offer =
		wl_container_of(listener, offer, source_destroy);

	offer->source = NULL;
}

static struct wlr_data_offer *wlr_data_source_send_offer(
		struct wlr_data_source *source,
		struct wl_resource *target) {
	struct wlr_data_offer *offer = calloc(1, sizeof(struct wlr_data_offer));

	offer->resource =
		wl_resource_create(wl_resource_get_client(target),
			&wl_data_offer_interface,
			wl_resource_get_version(target), 0);
	if (offer->resource == NULL) {
		free(offer);
		return NULL;
	}

	wl_resource_set_implementation(offer->resource, &data_offer_impl, offer,
		data_offer_resource_destroy);

	offer->source_destroy.notify = handle_offer_source_destroyed;
	wl_signal_add(&source->events.destroy, &offer->source_destroy);

	wl_data_device_send_data_offer(target, offer->resource);
	char **p;
	wl_array_for_each(p, &source->mime_types) {
		wl_data_offer_send_offer(offer->resource, *p);
	}

	offer->source = source;
	source->offer = offer;
	source->accepted = false;

	return offer;
}


void wlr_seat_handle_send_selection(struct wlr_seat_handle *handle) {
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

static void seat_handle_selection_data_source_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_seat *seat =
		wl_container_of(listener, seat, selection_data_source_destroy);

	// TODO send null selection to focused keyboard

	seat->selection_source = NULL;

	// TODO emit selection signal
}

static void wlr_seat_set_selection(struct wlr_seat *seat,
		struct wlr_data_source *source, uint32_t serial) {
	if (seat->selection_source &&
			seat->selection_serial - serial < UINT32_MAX / 2) {
		return;
	}

	if (seat->selection_source) {
		data_source_cancel(seat->selection_source);
		seat->selection_source = NULL;
		wl_list_remove(&seat->selection_data_source_destroy.link);
	}

	seat->selection_source = source;
	seat->selection_serial = serial;

	struct wlr_seat_handle *focused_handle =
		seat->keyboard_state.focused_handle;

	if (focused_handle) {
		wlr_seat_handle_send_selection(focused_handle);
	}

	// TODO emit selection signal

	if (source) {
		seat->selection_data_source_destroy.notify =
			seat_handle_selection_data_source_destroy;
		wl_signal_add(&source->events.destroy,
			&seat->selection_data_source_destroy);
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
