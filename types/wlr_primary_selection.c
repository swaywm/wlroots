#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <gtk-primary-selection-protocol.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/util/log.h>

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

static void device_manager_handle_create_source(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id) {
	//struct wlr_primary_selection_device_manager *manager =
	//	wl_resource_get_user_data(manager_resource);

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

	wl_signal_init(&source->events.destroy);
	wl_array_init(&source->mime_types);

	wlr_log(L_DEBUG, "new gtk_primary_selection_source %p (res %p)", source,
		source->resource);
}

static void device_manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct gtk_primary_selection_device_manager_interface
device_manager_impl = {
	.create_source = device_manager_handle_create_source,
	//.get_device = device_manager_handle_get_device,
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
