#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "types/wlr_data_device.h"
#include "util/signal.h"

static const struct wl_data_offer_interface data_offer_impl;

static struct wlr_data_offer *data_offer_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_data_offer_interface,
		&data_offer_impl));
	return wl_resource_get_user_data(resource);
}

static uint32_t data_offer_choose_action(struct wlr_data_offer *offer) {
	uint32_t offer_actions, preferred_action = 0;
	if (wl_resource_get_version(offer->resource) >=
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		offer_actions = offer->actions;
		preferred_action = offer->preferred_action;
	} else {
		offer_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	}

	uint32_t source_actions;
	if (offer->source->actions >= 0) {
		source_actions = offer->source->actions;
	} else {
		source_actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
	}

	uint32_t available_actions = offer_actions & source_actions;
	if (!available_actions) {
		return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
	}

	if (offer->source->compositor_action & available_actions) {
		return offer->source->compositor_action;
	}

	// If the dest side has a preferred DnD action, use it
	if ((preferred_action & available_actions) != 0) {
		return preferred_action;
	}

	// Use the first found action, in bit order
	return 1 << (ffs(available_actions) - 1);
}

void data_offer_update_action(struct wlr_data_offer *offer) {
	if (!offer->source) {
		return;
	}

	uint32_t action = data_offer_choose_action(offer);
	if (offer->source->current_dnd_action == action) {
		return;
	}

	offer->source->current_dnd_action = action;

	if (offer->in_ask) {
		return;
	}

	wlr_data_source_dnd_action(offer->source, action);

	if (wl_resource_get_version(offer->resource) >=
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		wl_data_offer_send_action(offer->resource, action);
	}
}

static void data_offer_accept(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial, const char *mime_type) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (!offer->source || offer != offer->source->offer) {
		return;
	}

	// TODO check that client is currently focused by the input device

	wlr_data_source_accept(offer->source, serial, mime_type);
}

static void data_offer_receive(struct wl_client *client,
		struct wl_resource *resource, const char *mime_type, int32_t fd) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (offer->source && offer == offer->source->offer) {
		wlr_data_source_send(offer->source, mime_type, fd);
	} else {
		close(fd);
	}
}

static void data_offer_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_offer_finish(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (!offer->source || offer->source->offer != offer) {
		return;
	}

	data_source_notify_finish(offer->source);
}

static void data_offer_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t actions,
		uint32_t preferred_action) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (actions & ~DATA_DEVICE_ALL_ACTIONS) {
		wl_resource_post_error(offer->resource,
			WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
			"invalid action mask %x", actions);
		return;
	}

	if (preferred_action && (!(preferred_action & actions) ||
			__builtin_popcount(preferred_action) > 1)) {
		wl_resource_post_error(offer->resource,
			WL_DATA_OFFER_ERROR_INVALID_ACTION,
			"invalid action %x", preferred_action);
		return;
	}

	offer->actions = actions;
	offer->preferred_action = preferred_action;

	data_offer_update_action(offer);
}

static void data_offer_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_data_offer *offer = data_offer_from_resource(resource);

	if (!offer->source) {
		goto out;
	}

	wl_list_remove(&offer->source_destroy.link);

	if (offer->source->offer != offer) {
		goto out;
	}

	// If the drag destination has version < 3, wl_data_offer.finish
	// won't be called, so do this here as a safety net, because
	// we still want the version >= 3 drag source to be happy.
	if (wl_resource_get_version(offer->resource) <
			WL_DATA_OFFER_ACTION_SINCE_VERSION) {
		data_source_notify_finish(offer->source);
		offer->source->offer = NULL;
	} else if (offer->source->impl->dnd_finish) {
		// source->cancel can free the source
		offer->source->offer = NULL;
		wlr_data_source_cancel(offer->source);
	} else {
		offer->source->offer = NULL;
	}

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

struct wlr_data_offer *data_offer_create(struct wl_client *client,
		struct wlr_data_source *source, uint32_t version) {
	struct wlr_data_offer *offer = calloc(1, sizeof(struct wlr_data_offer));
	if (offer == NULL) {
		return NULL;
	}
	offer->source = source;

	offer->resource = wl_resource_create(client,
		&wl_data_offer_interface, version, 0);
	if (offer->resource == NULL) {
		free(offer);
		return NULL;
	}
	wl_resource_set_implementation(offer->resource, &data_offer_impl, offer,
		data_offer_handle_resource_destroy);

	offer->source_destroy.notify = handle_offer_source_destroyed;
	wl_signal_add(&source->events.destroy, &offer->source_destroy);

	return offer;
}
