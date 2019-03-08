#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "wlr-output-management-unstable-v1-protocol.h"

#define OUTPUT_MANAGER_VERSION 1

enum {
	HEAD_STATE_ENABLED = 1 << 0,
	// TODO: other properties
};

static const uint32_t HEAD_STATE_ALL = HEAD_STATE_ENABLED;


// Can return NULL if the head is inert
static struct wlr_output_head_v1 *head_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_output_head_v1_interface, NULL));
	return wl_resource_get_user_data(resource);
}

static void head_destroy(struct wlr_output_head_v1 *head) {
	if (head == NULL) {
		return;
	}
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &head->resources) {
		zwlr_output_head_v1_send_finished(resource);
		wl_resource_set_user_data(resource, NULL); // make the resource inert
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}
	wl_list_remove(&head->link);
	wl_list_remove(&head->output_destroy.link);
	free(head);
}

static void head_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_head_v1 *head =
		wl_container_of(listener, head, output_destroy);
	head_destroy(head);
}

static struct wlr_output_head_v1 *head_create(
		struct wlr_output_manager_v1 *manager, struct wlr_output *output) {
	struct wlr_output_head_v1 *head = calloc(1, sizeof(*head));
	if (head == NULL) {
		return NULL;
	}
	head->manager = manager;
	head->state.output = output;
	wl_list_init(&head->resources);
	wl_list_insert(&manager->heads, &head->link);
	head->output_destroy.notify = head_handle_output_destroy;
	wl_signal_add(&output->events.destroy, &head->output_destroy);
	return head;
}


static void config_head_destroy(
		struct wlr_output_configuration_head_v1 *config_head) {
	if (config_head == NULL) {
		return;
	}
	if (config_head->resource != NULL) {
		wl_resource_set_user_data(config_head->resource, NULL); // make inert
	}
	wl_list_remove(&config_head->link);
	wl_list_remove(&config_head->output_destroy.link);
	free(config_head);
}

static void config_head_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_configuration_head_v1 *config_head =
		wl_container_of(listener, config_head, output_destroy);
	config_head_destroy(config_head);
}

struct wlr_output_configuration_head_v1 *
		wlr_output_configuration_head_v1_create(
		struct wlr_output_configuration_v1 *config, struct wlr_output *output) {
	struct wlr_output_configuration_head_v1 *config_head =
		calloc(1, sizeof(*config_head));
	if (config_head == NULL) {
		return NULL;
	}
	config_head->config = config;
	config_head->state.output = output;
	wl_list_insert(&config->heads, &config_head->link);
	config_head->output_destroy.notify = config_head_handle_output_destroy;
	wl_signal_add(&output->events.destroy, &config_head->output_destroy);
	return config_head;
}

static const struct zwlr_output_configuration_head_v1_interface config_head_impl;

// Can return NULL if the configuration head is inert
static struct wlr_output_configuration_head_v1 *config_head_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_output_head_v1_interface, &config_head_impl));
	return wl_resource_get_user_data(resource);
}

static const struct zwlr_output_configuration_head_v1_interface config_head_impl = {
	0 // TODO
};

static void config_head_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_output_configuration_head_v1 *config_head =
		config_head_from_resource(resource);
	config_head_destroy(config_head);
}


static const struct zwlr_output_configuration_v1_interface config_impl;

static struct wlr_output_configuration_v1 *config_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_output_configuration_v1_interface, &config_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_output_configuration_head_v1 *config_create_head(
		struct wlr_output_configuration_v1 *config, struct wlr_output *output) {
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		if (head->state.output == output) {
			return NULL; // TODO: post already_configured_head error instead
		}
	}
	return wlr_output_configuration_head_v1_create(config, output);
}

static void config_handle_enable_head(struct wl_client *client,
		struct wl_resource *config_resource, uint32_t id,
		struct wl_resource *head_resource) {
	struct wlr_output_configuration_v1 *config =
		config_from_resource(config_resource);
	// Can be NULL if the head no longer exists
	struct wlr_output_head_v1 *head = head_from_resource(head_resource);

	// Create an inert resource if the head no longer exists
	struct wlr_output_configuration_head_v1 *config_head = NULL;
	if (head != NULL) {
		config_head = config_create_head(config, head->state.output);
		if (config_head == NULL) {
			wl_resource_post_no_memory(config_resource);
			return;
		}
	}

	uint32_t version = wl_resource_get_version(config_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_output_configuration_head_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &config_head_impl,
		config_head, config_head_handle_resource_destroy);

	if (config_head != NULL) {
		config_head->resource = resource;
		config_head->state.enabled = true;
	}
}

static void config_handle_disable_head(struct wl_client *client,
		struct wl_resource *config_resource,
		struct wl_resource *head_resource) {
	struct wlr_output_configuration_v1 *config =
		config_from_resource(config_resource);
	struct wlr_output_head_v1 *head = head_from_resource(head_resource);
	if (head == NULL) {
		return;
	}

	struct wlr_output_configuration_head_v1 *config_head =
		config_create_head(config, head->state.output);
	if (config_head == NULL) {
		wl_resource_post_no_memory(config_resource);
		return;
	}

	config_head->state.enabled = false;
}

static void config_handle_apply(struct wl_client *client,
		struct wl_resource *config_resource) {
	//struct wlr_output_configuration_v1 *config =
	//	config_from_resource(config_resource);
	// TODO: post already_used if needed

	// TODO
}

static void config_handle_destroy(struct wl_client *client,
		struct wl_resource *config_resource) {
	wl_resource_destroy(config_resource);
	// TODO: destroy head configurations
}

static const struct zwlr_output_configuration_v1_interface config_impl = {
	.enable_head = config_handle_enable_head,
	.disable_head = config_handle_disable_head,
	.apply = config_handle_apply,
	// TODO: test
	.destroy = config_handle_destroy,
};

struct wlr_output_configuration_v1 *wlr_output_configuration_v1_create(void) {
	struct wlr_output_configuration_v1 *config = calloc(1, sizeof(*config));
	if (config == NULL) {
		return NULL;
	}
	wl_list_init(&config->heads);
	return config;
}

void wlr_output_configuration_v1_destroy(
		struct wlr_output_configuration_v1 *config) {
	if (config == NULL) {
		return;
	}
	struct wlr_output_configuration_head_v1 *head, *tmp;
	wl_list_for_each_safe(head, tmp, &config->heads, link) {
		config_head_destroy(head);
	}
	free(config);
}

static void config_handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_output_configuration_v1 *config = config_from_resource(resource);
	wlr_output_configuration_v1_destroy(config);
}


static void manager_handle_create_configuration(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id, uint32_t serial) {
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();
	config->serial = serial;

	uint32_t version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_output_configuration_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &config_impl,
		config, config_handle_resource_destroy);
}

static void manager_handle_stop(struct wl_client *client,
		struct wl_resource *manager_resource) {
	zwlr_output_manager_v1_send_finished(manager_resource);
	wl_resource_destroy(manager_resource);
}

static const struct zwlr_output_manager_v1_interface manager_impl = {
	.create_configuration = manager_handle_create_configuration,
	.stop = manager_handle_stop,
};

static void manager_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_bind(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	struct wlr_output_manager_v1 *manager = data;

	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_output_manager_v1_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager,
		manager_handle_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(resource));
}

static void manager_handle_display_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	struct wlr_output_head_v1 *head, *tmp;
	wl_list_for_each_safe(head, tmp, &manager->heads, link) {
		head_destroy(head);
	}
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_output_manager_v1 *wlr_output_manager_v1_create(
		struct wl_display *display) {
	struct wlr_output_manager_v1 *manager = calloc(1, sizeof(*manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->display = display;

	wl_signal_init(&manager->events.destroy);

	manager->global = wl_global_create(display,
		&zwlr_output_manager_v1_interface, OUTPUT_MANAGER_VERSION,
		manager, manager_bind);
	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	manager->display_destroy.notify = manager_handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}

static struct wlr_output_configuration_head_v1 *configuration_get_head(
		struct wlr_output_configuration_v1 *config, struct wlr_output *output) {
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		if (head->state.output == output) {
			return head;
		}
	}
	return NULL;
}

static void head_send_state(struct wlr_output_head_v1 *head,
		struct wl_resource *resource, uint32_t state) {
	if (state & HEAD_STATE_ENABLED) {
		zwlr_output_head_v1_send_enabled(resource, head->state.enabled);
	}

	if (!head->state.enabled) {
		return;
	}

	// TODO: send other properties
}

static void head_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_send_head(struct wlr_output_manager_v1 *manager,
		struct wlr_output_head_v1 *head, struct wl_resource *manager_resource) {
	struct wlr_output *output = head->state.output;

	struct wl_client *client = wl_resource_get_client(manager_resource);
	uint32_t version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_output_head_v1_interface, version, 0);
	if (resource == NULL) {
		wl_resource_post_no_memory(manager_resource);
		return;
	}
	wl_resource_set_implementation(resource, NULL, head,
		head_handle_resource_destroy);
	wl_list_insert(&head->resources, wl_resource_get_link(resource));

	zwlr_output_manager_v1_send_head(manager_resource, resource);

	// TODO: send modes

	zwlr_output_head_v1_send_name(resource, output->name);

	char description[128];
	snprintf(description, sizeof(description), "%s %s %s (%s)",
		output->make, output->model, output->serial, output->name);
	zwlr_output_head_v1_send_description(resource, description);

	if (output->phys_width > 0 && output->phys_height > 0) {
		zwlr_output_head_v1_send_physical_size(resource,
			output->phys_width, output->phys_height);
	}

	head_send_state(head, resource, HEAD_STATE_ALL);
}

static void manager_update_head(struct wlr_output_manager_v1 *manager,
		struct wlr_output_head_v1 *head,
		struct wlr_output_head_v1_state *next) {
	struct wlr_output_head_v1_state *current = &head->state;

	uint32_t state = 0;
	if (current->enabled != next->enabled) {
		state |= HEAD_STATE_ENABLED;
		current->enabled = next->enabled;
	}
	// TODO: update other properties

	struct wl_resource *resource;
	wl_resource_for_each(resource, &head->resources) {
		head_send_state(head, resource, state);
	}
}

void wlr_output_manager_v1_set_configuration(
		struct wlr_output_manager_v1 *manager,
		struct wlr_output_configuration_v1 *config) {
	// Either update or destroy existing heads
	struct wlr_output_head_v1 *existing_head, *head_tmp;
	wl_list_for_each_safe(existing_head, head_tmp, &manager->heads, link) {
		struct wlr_output_configuration_head_v1 *updated_head =
			configuration_get_head(config, existing_head->state.output);
		if (updated_head != NULL) {
			manager_update_head(manager, existing_head, &updated_head->state);
			config_head_destroy(updated_head);
		} else {
			head_destroy(existing_head);
		}
	}

	// Heads remaining in `config` are new heads

	// Move new heads to current config
	struct wlr_output_configuration_head_v1 *config_head, *config_head_tmp;
	wl_list_for_each_safe(config_head, config_head_tmp, &config->heads, link) {
		struct wlr_output_head_v1 *head =
			head_create(manager, config_head->state.output);
		if (head == NULL) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			continue;
		}

		head->state = config_head->state;

		struct wl_resource *manager_resource;
		wl_resource_for_each(manager_resource, &manager->resources) {
			manager_send_head(manager, head, manager_resource);
		}
	}

	wlr_output_configuration_v1_destroy(config);

	manager->serial = wl_display_next_serial(manager->display);
	struct wl_resource *manager_resource;
	wl_resource_for_each(manager_resource, &manager->resources) {
		zwlr_output_manager_v1_send_done(manager_resource,
			manager->serial);
	}
}
