#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_output.h>
#include <wlr/util/log.h>
#include "xdg-output-unstable-v1-protocol.h"

static void xdg_output_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zxdg_output_v1_interface xdg_output_implementation = {
	.destroy = xdg_output_destroy,
};

static void xdg_output_manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zxdg_output_manager_v1_interface xdg_output_manager_implementation;

static void xdg_output_send_details(struct wl_resource *resource,
		struct wlr_output_layout_output *layout_output) {
	zxdg_output_v1_send_logical_position(resource,
			layout_output->x, layout_output->y);
	zxdg_output_v1_send_logical_size(resource,
			(float)layout_output->output->width / layout_output->output->scale,
			(float)layout_output->output->height / layout_output->output->scale);
	zxdg_output_v1_send_done(resource);
}

static void xdg_output_manager_get_xdg_output(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *output) {
	assert(wl_resource_instance_of(resource, &zxdg_output_manager_v1_interface,
			&xdg_output_manager_implementation));

	struct wlr_xdg_output_manager *manager =
		wl_resource_get_user_data(resource);
	struct wlr_output_layout *layout = manager->layout;
	struct wlr_output *wlr_output = wlr_output_from_resource(output);

	struct wlr_output_layout_output *layout_output =
		wlr_output_layout_get(layout, wlr_output);
	assert(layout_output);

	struct wlr_xdg_output *_xdg_output, *xdg_output = NULL;
	wl_list_for_each(_xdg_output, &manager->outputs, link) {
		if (_xdg_output->layout_output == layout_output) {
			xdg_output = _xdg_output;
			break;
		}
	}
	assert(xdg_output);

	struct wl_resource *output_resource = wl_resource_create(client,
			&zxdg_output_v1_interface, wl_resource_get_version(resource), id);
	if (!output_resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(output_resource,
			&xdg_output_implementation, NULL, NULL);
	xdg_output_send_details(output_resource, layout_output);
	wl_list_insert(&xdg_output->resources,
			wl_resource_get_link(output_resource));
}

static const struct zxdg_output_manager_v1_interface xdg_output_manager_implementation = {
	.destroy = xdg_output_manager_destroy,
	.get_xdg_output = xdg_output_manager_get_xdg_output,
};

static void output_manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_xdg_output_manager *manager = data;
	assert(wl_client && manager);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&zxdg_output_manager_v1_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource,
			&xdg_output_manager_implementation, manager, NULL);
}

static void destroy_output(struct wlr_xdg_output *output) {
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &output->resources) {
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xdg_output *output = wl_container_of(listener, output, destroy);
	destroy_output(output);
}

static void add_output(struct wlr_xdg_output_manager *manager,
		struct wlr_output_layout_output *layout_output) {
	struct wlr_xdg_output *output = calloc(1, sizeof(struct wlr_xdg_output));
	wl_list_init(&output->resources);
	output->manager = manager;
	output->layout_output = layout_output;
	output->destroy.notify = handle_output_destroy;
	wl_signal_add(&layout_output->events.destroy, &output->destroy);
	wl_list_insert(&manager->outputs, &output->link);
}

static void xdg_output_manager_send_details(
		struct wlr_xdg_output_manager *manager) {
	struct wlr_xdg_output *output;
	wl_list_for_each(output, &manager->outputs, link) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &output->resources) {
			xdg_output_send_details(resource, output->layout_output);
		}
	}
}

static void handle_layout_add(struct wl_listener *listener, void *data) {
	struct wlr_xdg_output_manager *manager =
		wl_container_of(listener, manager, layout_add);
	struct wlr_output_layout_output *layout_output = data;
	add_output(manager, layout_output);
}

static void handle_layout_change(struct wl_listener *listener, void *data) {
	struct wlr_xdg_output_manager *manager =
		wl_container_of(listener, manager, layout_change);
	xdg_output_manager_send_details(manager);
}

static void handle_layout_destroy(struct wl_listener *listener, void *data) {
	struct wlr_xdg_output_manager *manager =
		wl_container_of(listener, manager, layout_change);
	wlr_xdg_output_manager_destroy(manager);
}

struct wlr_xdg_output_manager *wlr_xdg_output_manager_create(
		struct wl_display *display, struct wlr_output_layout *layout) {
	assert(display && layout);
	struct wlr_xdg_output_manager *manager =
		calloc(1, sizeof(struct wlr_xdg_output_manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->layout = layout;
	manager->global = wl_global_create(display,
			&zxdg_output_manager_v1_interface,
			1, manager, output_manager_bind);
	if (!manager->global) {
		free(manager);
		return NULL;
	}

	wl_list_init(&manager->outputs);
	struct wlr_output_layout_output *layout_output;
	wl_list_for_each(layout_output, &layout->outputs, link) {
		add_output(manager, layout_output);
	}

	manager->layout_add.notify = handle_layout_add;
	wl_signal_add(&layout->events.add, &manager->layout_add);
	manager->layout_change.notify = handle_layout_change;
	wl_signal_add(&layout->events.change, &manager->layout_change);
	manager->layout_destroy.notify = handle_layout_destroy;
	wl_signal_add(&layout->events.destroy, &manager->layout_destroy);
	return manager;
}

void wlr_xdg_output_manager_destroy(struct wlr_xdg_output_manager *manager) {
	struct wlr_xdg_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &manager->outputs, link) {
		destroy_output(output);
	}
	wl_list_remove(&manager->layout_add.link);
	wl_list_remove(&manager->layout_change.link);
	wl_list_remove(&manager->layout_destroy.link);
	free(manager);
}
