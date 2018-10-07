#define _XOPEN_SOURCE 700
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "types/wlr_data_device.h"
#include "util/signal.h"

void data_source_notify_finish(struct wlr_data_source *source) {
	assert(source->offer);
	if (source->actions < 0) {
		return;
	}

	if (source->offer->in_ask) {
		wlr_data_source_dnd_action(source, source->current_dnd_action);
	}

	source->offer = NULL;
	wlr_data_source_dnd_finish(source);
}

struct wlr_data_offer *data_source_send_offer(struct wlr_data_source *source,
		struct wlr_seat_client *target) {
	if (wl_list_empty(&target->data_devices)) {
		return NULL;
	}

	uint32_t version = wl_resource_get_version(
		wl_resource_from_link(target->data_devices.next));

	struct wlr_data_offer *offer =
		data_offer_create(target->client, source, version);
	if (offer == NULL) {
		return NULL;
	}

	struct wl_resource *target_resource;
	wl_resource_for_each(target_resource, &target->data_devices) {
		wl_data_device_send_data_offer(target_resource, offer->resource);
	}

	char **p;
	wl_array_for_each(p, &source->mime_types) {
		wl_data_offer_send_offer(offer->resource, *p);
	}

	source->offer = offer;
	source->accepted = false;
	return offer;
}

void wlr_data_source_init(struct wlr_data_source *source,
		const struct wlr_data_source_impl *impl) {
	assert(impl->send);

	source->impl = impl;
	wl_array_init(&source->mime_types);
	wl_signal_init(&source->events.destroy);
	source->actions = -1;
}

void wlr_data_source_finish(struct wlr_data_source *source) {
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

void wlr_data_source_send(struct wlr_data_source *source, const char *mime_type,
		int32_t fd) {
	source->impl->send(source, mime_type, fd);
}

void wlr_data_source_accept(struct wlr_data_source *source, uint32_t serial,
		const char *mime_type) {
	source->accepted = (mime_type != NULL);
	if (source->impl->accept) {
		source->impl->accept(source, serial, mime_type);
	}
}

void wlr_data_source_cancel(struct wlr_data_source *source) {
	if (source->impl->cancel) {
		source->impl->cancel(source);
	}
}

void wlr_data_source_dnd_drop(struct wlr_data_source *source) {
	if (source->impl->dnd_drop) {
		source->impl->dnd_drop(source);
	}
}

void wlr_data_source_dnd_finish(struct wlr_data_source *source) {
	if (source->impl->dnd_finish) {
		source->impl->dnd_finish(source);
	}
}

void wlr_data_source_dnd_action(struct wlr_data_source *source,
		enum wl_data_device_manager_dnd_action action) {
	source->current_dnd_action = action;
	if (source->impl->dnd_action) {
		source->impl->dnd_action(source, action);
	}
}


static const struct wl_data_source_interface data_source_impl;

struct wlr_client_data_source *client_data_source_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_data_source_interface,
		&data_source_impl));
	return wl_resource_get_user_data(resource);
}

static void client_data_source_accept(struct wlr_data_source *wlr_source,
	uint32_t serial, const char *mime_type);

static struct wlr_client_data_source *client_data_source_from_wlr_data_source(
		struct wlr_data_source *wlr_source) {
	assert(wlr_source->impl->accept == client_data_source_accept);
	return (struct wlr_client_data_source *)wlr_source;
}

static void client_data_source_accept(struct wlr_data_source *wlr_source,
		uint32_t serial, const char *mime_type) {
	struct wlr_client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	wl_data_source_send_target(source->resource, mime_type);
}

static void client_data_source_send(struct wlr_data_source *wlr_source,
		const char *mime_type, int32_t fd) {
	struct wlr_client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	wl_data_source_send_send(source->resource, mime_type, fd);
	close(fd);
}

static void client_data_source_cancel(struct wlr_data_source *wlr_source) {
	struct wlr_client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	wl_data_source_send_cancelled(source->resource);
}

static void client_data_source_dnd_drop(struct wlr_data_source *wlr_source) {
	struct wlr_client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	assert(wl_resource_get_version(source->resource) >=
		WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION);
	wl_data_source_send_dnd_drop_performed(source->resource);
}

static void client_data_source_dnd_finish(struct wlr_data_source *wlr_source) {
	struct wlr_client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	assert(wl_resource_get_version(source->resource) >=
		WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION);
	wl_data_source_send_dnd_finished(source->resource);
}

static void client_data_source_dnd_action(struct wlr_data_source *wlr_source,
		enum wl_data_device_manager_dnd_action action) {
	struct wlr_client_data_source *source =
		client_data_source_from_wlr_data_source(wlr_source);
	assert(wl_resource_get_version(source->resource) >=
		WL_DATA_SOURCE_ACTION_SINCE_VERSION);
	wl_data_source_send_action(source->resource, action);
}

static void data_source_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions) {
	struct wlr_client_data_source *source =
		client_data_source_from_resource(resource);

	if (source->source.actions >= 0) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"cannot set actions more than once");
		return;
	}

	if (dnd_actions & ~DATA_DEVICE_ALL_ACTIONS) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"invalid action mask %x", dnd_actions);
		return;
	}

	if (source->finalized) {
		wl_resource_post_error(source->resource,
			WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
			"invalid action change after wl_data_device.start_drag");
		return;
	}

	source->source.actions = dnd_actions;
}

static void data_source_offer(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type) {
	struct wlr_client_data_source *source =
		client_data_source_from_resource(resource);

	char **p = wl_array_add(&source->source.mime_types, sizeof(*p));
	if (p) {
		*p = strdup(mime_type);
	}
	if (!p || !*p) {
		if (p) {
			source->source.mime_types.size -= sizeof(*p);
		}
		wl_resource_post_no_memory(resource);
	}
}

static const struct wl_data_source_interface data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void data_source_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_client_data_source *source =
		client_data_source_from_resource(resource);
	wlr_data_source_finish(&source->source);
	wl_list_remove(wl_resource_get_link(source->resource));
	free(source);
}

struct wlr_client_data_source *client_data_source_create(
		struct wl_client *client, uint32_t version, uint32_t id,
		struct wl_list *resource_list) {
	struct wlr_client_data_source *source =
		calloc(1, sizeof(struct wlr_client_data_source));
	if (source == NULL) {
		return NULL;
	}

	source->resource = wl_resource_create(client, &wl_data_source_interface,
		version, id);
	if (source->resource == NULL) {
		wl_resource_post_no_memory(source->resource);
		free(source);
		return NULL;
	}
	wl_resource_set_implementation(source->resource, &data_source_impl,
		source, data_source_handle_resource_destroy);
	wl_list_insert(resource_list, wl_resource_get_link(source->resource));

	source->impl.accept = client_data_source_accept;
	source->impl.send = client_data_source_send;
	source->impl.cancel = client_data_source_cancel;

	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_DND_DROP_PERFORMED_SINCE_VERSION) {
		source->impl.dnd_drop = client_data_source_dnd_drop;
	}
	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
		source->impl.dnd_finish = client_data_source_dnd_finish;
	}
	if (wl_resource_get_version(source->resource) >=
			WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
		source->impl.dnd_action = client_data_source_dnd_action;
	}

	wlr_data_source_init(&source->source, &source->impl);
	return source;
}
