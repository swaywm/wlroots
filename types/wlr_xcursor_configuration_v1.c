#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_configuration_v1.h>
#include <wlr/util/log.h>
#include "util/signal.h"

#define XCURSOR_CONFIGURATION_MANAGER_VERSION 1

static struct wlr_xcursor_configuration_v1 *manager_get_configuration(
		struct wlr_xcursor_configuration_manager_v1 *manager,
		struct wlr_seat *seat,
		enum zwp_xcursor_configuration_manager_v1_device_type device_type) {
	struct wlr_xcursor_configuration_v1 *config;
	wl_list_for_each(config, &manager->configurations, link) {
		if (config->seat == seat && config->device_type == device_type) {
			return config;
		}
	}
	return NULL;
}

static void configuration_handle_destroy(struct wl_client *client,
		struct wl_resource *config_resource) {
	wl_resource_destroy(config_resource);
}

static const struct zwp_xcursor_configuration_v1_interface
		configuration_impl = {
	.destroy = configuration_handle_destroy,
};

static void configuration_send(struct wlr_xcursor_configuration_v1 *config,
		struct wl_resource *resource) {
	zwp_xcursor_configuration_v1_send_theme(resource,
		config->attrs.theme.name, config->attrs.theme.size);
	zwp_xcursor_configuration_v1_send_default_cursor(resource,
		config->attrs.default_cursor);
	zwp_xcursor_configuration_v1_send_done(resource);
}

static void configuration_handle_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static const struct zwp_xcursor_configuration_manager_v1_interface manager_impl;

struct wlr_xcursor_configuration_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_xcursor_configuration_manager_v1_interface, &manager_impl));
	return wl_resource_get_user_data(resource);
}

static void manager_handle_get_device_xcursor_configuration(
		struct wl_client *client, struct wl_resource *manager_resource,
		uint32_t id, struct wl_resource *seat_resource,
		enum zwp_xcursor_configuration_manager_v1_device_type device_type) {
	struct wlr_xcursor_configuration_manager_v1 *manager =
		manager_from_resource(manager_resource);
	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);

	struct wlr_xcursor_configuration_v1 *config =
		manager_get_configuration(manager, seat_client->seat, device_type);
	assert(config != NULL);

	uint32_t version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwp_xcursor_configuration_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &configuration_impl, config,
		configuration_handle_resource_destroy);

	wl_list_insert(&config->resources, wl_resource_get_link(resource));

	configuration_send(config, resource);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource) {
	wl_resource_destroy(manager_resource);
}

static const struct zwp_xcursor_configuration_manager_v1_interface
		manager_impl = {
	.get_device_xcursor_configuration =
		manager_handle_get_device_xcursor_configuration,
	.destroy = manager_handle_destroy,
};

static void manager_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	struct wlr_xcursor_configuration_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwp_xcursor_configuration_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager,
		manager_handle_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xcursor_configuration_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_xcursor_configuration_manager_v1_destroy(manager);
}

struct wlr_xcursor_configuration_manager_v1 *
		wlr_xcursor_configuration_manager_v1_create(struct wl_display *display) {
	struct wlr_xcursor_configuration_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_xcursor_configuration_manager_v1));
	if (manager == NULL) {
		return NULL;
	}

	manager->global = wl_global_create(display,
		&zwp_xcursor_configuration_manager_v1_interface,
		XCURSOR_CONFIGURATION_MANAGER_VERSION, manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	wl_list_init(&manager->resources);
	wl_list_init(&manager->configurations);
	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

static void configuration_destroy(struct wlr_xcursor_configuration_v1 *config) {
	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &config->resources) {
		// Make resource inert
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}
	wl_list_remove(&config->seat_destroy.link);
	free(config->attrs.theme.name);
	free(config->attrs.default_cursor);
	free(config);
}

void wlr_xcursor_configuration_manager_v1_destroy(
		struct wlr_xcursor_configuration_manager_v1 *manager) {
	if (manager == NULL) {
		return;
	}
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	struct wlr_xcursor_configuration_v1 *config, *tmp_config;
	wl_list_for_each_safe(config, tmp_config, &manager->configurations, link) {
		struct wl_resource *resource, *tmp_resource;
		wl_resource_for_each_safe(resource, tmp_resource, &config->resources) {
			wl_resource_destroy(resource);
		}
		configuration_destroy(config);
	}
	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &manager->resources) {
		wl_resource_destroy(resource);
	}
	wl_global_destroy(manager->global);
	free(manager);
}

static void configuration_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_xcursor_configuration_v1 *config =
		wl_container_of(listener, config, seat_destroy);
	configuration_destroy(config);
}

void wlr_xcursor_configuration_manager_v1_configure(
		struct wlr_xcursor_configuration_manager_v1 *manager,
		struct wlr_seat *seat,
		enum zwp_xcursor_configuration_manager_v1_device_type device_type,
		const struct wlr_xcursor_configuration_v1_attrs *attrs) {
	struct wlr_xcursor_configuration_v1 *config =
		manager_get_configuration(manager, seat, device_type);
	if (config == NULL) {
		config = calloc(1, sizeof(struct wlr_xcursor_configuration_v1));
		if (config == NULL) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return;
		}
		config->seat = seat;
		config->device_type = device_type;
		wl_list_init(&config->resources);
		wl_list_insert(&manager->configurations, &config->link);

		config->seat_destroy.notify = configuration_handle_seat_destroy;
		wl_signal_add(&seat->events.destroy, &config->seat_destroy);
	}

	assert(attrs->theme.name != NULL);
	assert(attrs->theme.size != 0);
	assert(attrs->default_cursor != NULL);

	free(config->attrs.theme.name);
	config->attrs.theme.name = strdup(attrs->theme.name);
	config->attrs.theme.size = attrs->theme.size;
	free(config->attrs.default_cursor);
	config->attrs.default_cursor = strdup(attrs->default_cursor);

	struct wl_resource *resource;
	wl_resource_for_each(resource, &config->resources) {
		configuration_send(config, resource);
	}
}
