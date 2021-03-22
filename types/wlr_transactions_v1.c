#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_transactions_v1.h>
#include "transactions-unstable-v1-protocol.h"

#define TRANSACTION_MANAGER_V1_VERSION 1

static const struct zwp_transaction_v1_interface transaction_impl;

static struct wlr_transaction_v1 *transaction_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_transaction_v1_interface, &transaction_impl));
	return wl_resource_get_user_data(resource);
}

static void transaction_surface_destroy(
		struct wlr_transaction_v1_surface *transaction_surface) {
	if (transaction_surface == NULL) {
		return;
	}

	wl_list_remove(&transaction_surface->surface_destroy.link);
	wl_list_remove(&transaction_surface->link);
	free(transaction_surface);
}

static void transaction_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_transaction_v1 *transaction =
		transaction_from_resource(resource);

	struct wlr_transaction_v1_surface *transaction_surface, *tmp;
	wl_list_for_each_safe(transaction_surface, tmp, &transaction->surfaces, link) {
		wlr_surface_unlock_cached(transaction_surface->surface,
			transaction_surface->cached_state_seq);
		transaction_surface_destroy(transaction_surface);
	}

	free(transaction);
}

static void transaction_surface_handle_surface_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_transaction_v1_surface *transaction_surface =
		wl_container_of(listener, transaction_surface, surface_destroy);
	transaction_surface_destroy(transaction_surface);
}

static void transaction_handle_add_surface(struct wl_client *client,
		struct wl_resource *transaction_resource,
		struct wl_resource *surface_resource) {
	struct wlr_transaction_v1 *transaction =
		transaction_from_resource(transaction_resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	struct wlr_transaction_v1_surface *transaction_surface =
		calloc(1, sizeof(*transaction_surface));
	if (transaction_surface == NULL) {
		wl_resource_post_no_memory(transaction_resource);
		return;
	}

	transaction_surface->surface = surface;
	transaction_surface->cached_state_seq = wlr_surface_lock_pending(surface);

	transaction_surface->surface_destroy.notify = transaction_surface_handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &transaction_surface->surface_destroy);

	wl_list_insert(&transaction->surfaces, &transaction_surface->link);
}

static void transaction_handle_commit(struct wl_client *client,
		struct wl_resource *transaction_resource) {
	// The resource destroy handler will release surface locks
	wl_resource_destroy(transaction_resource);
}

static const struct zwp_transaction_v1_interface transaction_impl = {
	.add_surface = transaction_handle_add_surface,
	.commit = transaction_handle_commit,
};

static void manager_handle_create_transaction(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id) {
	struct wlr_transaction_v1 *transaction = calloc(1, sizeof(*transaction));
	if (transaction == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}

	wl_list_init(&transaction->surfaces);

	uint32_t version = wl_resource_get_version(manager_resource);
	transaction->resource = wl_resource_create(client,
		&zwp_transaction_v1_interface, version, id);
	if (transaction->resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		free(transaction);
		return;
	}
	wl_resource_set_implementation(transaction->resource, &transaction_impl,
		transaction, transaction_handle_resource_destroy);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwp_transaction_manager_v1_interface manager_impl = {
	.destroy = manager_handle_destroy,
	.create_transaction = manager_handle_create_transaction,
};

static void manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_transaction_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwp_transaction_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_transaction_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wl_global_destroy(manager->global);
	wl_list_remove(&manager->display_destroy.link);
	free(manager);
}

struct wlr_transaction_manager_v1 *wlr_transaction_manager_v1_create(
		struct wl_display *display) {
	struct wlr_transaction_manager_v1 *manager = calloc(1, sizeof(*manager));
	if (manager == NULL) {
		return NULL;
	}

	manager->global = wl_global_create(display,
		&zwp_transaction_manager_v1_interface, TRANSACTION_MANAGER_V1_VERSION,
		manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
