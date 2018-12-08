#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "wlr-foreign-toplevel-management-unstable-v1-protocol.h"

#define FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION 1

static const struct zwlr_foreign_toplevel_handle_v1_interface toplevel_handle_impl;

static struct wlr_foreign_toplevel_handle_v1 *toplevel_handle_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zwlr_foreign_toplevel_handle_v1_interface,
			&toplevel_handle_impl));
	return wl_resource_get_user_data(resource);
}

static void toplevel_handle_send_maximized_event(struct wl_resource *resource,
		bool state) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel =
		toplevel_handle_from_resource(resource);
	if (!toplevel) {
		return;
	}
	struct wlr_foreign_toplevel_handle_v1_maximized_event event = {
		.toplevel = toplevel,
		.maximized = state,
	};
	wlr_signal_emit_safe(&toplevel->events.request_maximize, &event);
}

void foreign_toplevel_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	toplevel_handle_send_maximized_event(resource, true);
}

void foreign_toplevel_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource) {
	toplevel_handle_send_maximized_event(resource, false);
}

static void toplevel_send_minimized_event(struct wl_resource *resource,
		bool state) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel =
		toplevel_handle_from_resource(resource);
	if (!toplevel) {
		return;
	}

	struct wlr_foreign_toplevel_handle_v1_minimized_event event = {
		.toplevel = toplevel,
		.minimized = state,
	};
	wlr_signal_emit_safe(&toplevel->events.request_minimize, &event);
}

static void foreign_toplevel_handle_set_minimized(struct wl_client *client,
		struct wl_resource *resource) {
	toplevel_send_minimized_event(resource, true);
}

static void foreign_toplevel_handle_unset_minimized(struct wl_client *client,
		struct wl_resource *resource) {
	toplevel_send_minimized_event(resource, false);
}

static void foreign_toplevel_handle_activate(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel =
		toplevel_handle_from_resource(resource);
	if (!toplevel) {
		return;
	}

	struct wlr_seat_client *seat_client = wlr_seat_client_from_resource(seat);
	struct wlr_foreign_toplevel_handle_v1_activated_event event = {
		.toplevel = toplevel,
		.seat = seat_client->seat,
	};
	wlr_signal_emit_safe(&toplevel->events.request_activate, &event);
}

static void foreign_toplevel_handle_close(struct wl_client *client,
		struct wl_resource *resource) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel =
		toplevel_handle_from_resource(resource);
	if (!toplevel) {
		return;
	}
	wlr_signal_emit_safe(&toplevel->events.request_close, toplevel);
}

static void foreign_toplevel_handle_set_rectangle(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *surface,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel =
		toplevel_handle_from_resource(resource);
	if (!toplevel) {
		return;
	}

	if (width < 0 || height < 0) {
		wl_resource_post_error(resource,
			ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_ERROR_INVALID_RECTANGLE,
			"invalid rectangle passed to set_rectangle: width/height < 0");
		return;
	}

	struct wlr_foreign_toplevel_handle_v1_set_rectangle_event event = {
		.toplevel = toplevel,
		.surface = wlr_surface_from_resource(surface),
		.x = x,
		.y = y,
		.width = width,
		.height = height,
	};
	wlr_signal_emit_safe(&toplevel->events.set_rectangle, &event);
}

static void foreign_toplevel_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_handle_v1_interface toplevel_handle_impl = {
	.set_maximized = foreign_toplevel_handle_set_maximized,
	.unset_maximized = foreign_toplevel_handle_unset_maximized,
	.set_minimized = foreign_toplevel_handle_set_minimized,
	.unset_minimized = foreign_toplevel_handle_unset_minimized,
	.activate = foreign_toplevel_handle_activate,
	.close = foreign_toplevel_handle_close,
	.set_rectangle = foreign_toplevel_handle_set_rectangle,
	.destroy = foreign_toplevel_handle_destroy
};

static void toplevel_idle_send_done(void *data) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel = data;
	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zwlr_foreign_toplevel_handle_v1_send_done(resource);
	}

	toplevel->idle_source = NULL;
}

static void toplevel_update_idle_source(
	struct wlr_foreign_toplevel_handle_v1 *toplevel) {
	if (toplevel->idle_source) {
		return;
	}

	toplevel->idle_source = wl_event_loop_add_idle(toplevel->manager->event_loop,
		toplevel_idle_send_done, toplevel);
}

void wlr_foreign_toplevel_handle_v1_set_title(
		struct wlr_foreign_toplevel_handle_v1 *toplevel, const char *title) {
	free(toplevel->title);
	toplevel->title = strdup(title);

	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zwlr_foreign_toplevel_handle_v1_send_title(resource, title);
	}

	toplevel_update_idle_source(toplevel);
}

void wlr_foreign_toplevel_handle_v1_set_app_id(
		struct wlr_foreign_toplevel_handle_v1 *toplevel, const char *app_id) {
	free(toplevel->app_id);
	toplevel->app_id = strdup(app_id);

	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zwlr_foreign_toplevel_handle_v1_send_app_id(resource, app_id);
	}

	toplevel_update_idle_source(toplevel);
}

static void send_output_to_resource(struct wl_resource *resource,
		struct wlr_output *output, bool enter) {
	struct wl_client *client = wl_resource_get_client(resource);
	struct wl_resource *output_resource;

	wl_resource_for_each(output_resource, &output->resources) {
		if (wl_resource_get_client(output_resource) == client) {
			if (enter) {
				zwlr_foreign_toplevel_handle_v1_send_output_enter(resource,
					output_resource);
			} else {
				zwlr_foreign_toplevel_handle_v1_send_output_leave(resource,
					output_resource);
			}
		}
	}
}

static void toplevel_send_output(struct wlr_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_output *output, bool enter) {
	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		send_output_to_resource(resource, output, enter);
	}

	toplevel_update_idle_source(toplevel);
}

static void toplevel_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_foreign_toplevel_handle_v1_output *toplevel_output =
		wl_container_of(listener, toplevel_output, output_destroy);
	wlr_foreign_toplevel_handle_v1_output_leave(toplevel_output->toplevel,
		toplevel_output->output);
}

void wlr_foreign_toplevel_handle_v1_output_enter(
		struct wlr_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_output *output) {
	struct wlr_foreign_toplevel_handle_v1_output *toplevel_output;
	wl_list_for_each(toplevel_output, &toplevel->outputs, link) {
		if (toplevel_output->output == output) {
			return; // we have already sent output_enter event
		}
	}

	toplevel_output =
	    calloc(1, sizeof(struct wlr_foreign_toplevel_handle_v1_output));
	if (!toplevel_output) {
		wlr_log(WLR_ERROR, "failed to allocate memory for toplevel output");
		return;
	}

	toplevel_output->output = output;
	toplevel_output->toplevel = toplevel;
	wl_list_insert(&toplevel->outputs, &toplevel_output->link);

	toplevel_output->output_destroy.notify = toplevel_handle_output_destroy;
	wl_signal_add(&output->events.destroy, &toplevel_output->output_destroy);

	toplevel_send_output(toplevel, output, true);
}

static void toplevel_output_destroy(
		struct wlr_foreign_toplevel_handle_v1_output *toplevel_output) {
	wl_list_remove(&toplevel_output->link);
	wl_list_remove(&toplevel_output->output_destroy.link);
	free(toplevel_output);
}

void wlr_foreign_toplevel_handle_v1_output_leave(
		struct wlr_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_output *output) {
	struct wlr_foreign_toplevel_handle_v1_output *toplevel_output_iterator;
	struct wlr_foreign_toplevel_handle_v1_output *toplevel_output = NULL;

	wl_list_for_each(toplevel_output_iterator, &toplevel->outputs, link) {
		if (toplevel_output_iterator->output == output) {
			toplevel_output = toplevel_output_iterator;
			break;
		}
	}

	if (toplevel_output) {
		toplevel_send_output(toplevel, output, false);
		toplevel_output_destroy(toplevel_output);
	} else {
		// XXX: log an error? crash?
	}
}

static bool fill_array_from_toplevel_state(struct wl_array *array,
		uint32_t state) {
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	}
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	}

	return true;
}

static void toplevel_send_state(struct wlr_foreign_toplevel_handle_v1 *toplevel) {
	struct wl_array states;
	wl_array_init(&states);
	bool r = fill_array_from_toplevel_state(&states, toplevel->state);
	if (!r) {
		struct wl_resource *resource;
		wl_resource_for_each(resource, &toplevel->resources) {
			wl_resource_post_no_memory(resource);
		}

		wl_array_release(&states);
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zwlr_foreign_toplevel_handle_v1_send_state(resource, &states);
	}

	wl_array_release(&states);
	toplevel_update_idle_source(toplevel);
}

void wlr_foreign_toplevel_handle_v1_set_maximized(
		struct wlr_foreign_toplevel_handle_v1 *toplevel, bool maximized) {
	if (maximized) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	}
	toplevel_send_state(toplevel);
}

void wlr_foreign_toplevel_handle_v1_set_minimized(
		struct wlr_foreign_toplevel_handle_v1 *toplevel, bool minimized) {
	if (minimized) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
	toplevel_send_state(toplevel);
}

void wlr_foreign_toplevel_handle_v1_set_activated(
		struct wlr_foreign_toplevel_handle_v1 *toplevel, bool activated) {
	if (activated) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	}
	toplevel_send_state(toplevel);
}

void wlr_foreign_toplevel_handle_v1_destroy(
		struct wlr_foreign_toplevel_handle_v1 *toplevel) {
	if (!toplevel) {
		return;
	}

	wlr_signal_emit_safe(&toplevel->events.destroy, toplevel);

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &toplevel->resources) {
		zwlr_foreign_toplevel_handle_v1_send_closed(resource);
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}

	struct wlr_foreign_toplevel_handle_v1_output *toplevel_output, *tmp2;
	wl_list_for_each_safe(toplevel_output, tmp2, &toplevel->outputs, link) {
		toplevel_output_destroy(toplevel_output);
	}

	if (toplevel->idle_source) {
		wl_event_source_remove(toplevel->idle_source);
	}

	wl_list_remove(&toplevel->link);

	free(toplevel->title);
	free(toplevel->app_id);
	free(toplevel);
}

static void foreign_toplevel_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static struct wl_resource *create_toplevel_resource_for_resource(
		struct wlr_foreign_toplevel_handle_v1 *toplevel,
		struct wl_resource *manager_resource) {
	struct wl_client *client = wl_resource_get_client(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
			&zwlr_foreign_toplevel_handle_v1_interface,
			wl_resource_get_version(manager_resource), 0);
	if (!resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

	wl_resource_set_implementation(resource, &toplevel_handle_impl, toplevel,
	    foreign_toplevel_resource_destroy);

	wl_list_insert(&toplevel->resources, wl_resource_get_link(resource));
	zwlr_foreign_toplevel_manager_v1_send_toplevel(manager_resource, resource);
	return resource;
}

struct wlr_foreign_toplevel_handle_v1 *
wlr_foreign_toplevel_handle_v1_create(
		struct wlr_foreign_toplevel_manager_v1 *manager) {
	struct wlr_foreign_toplevel_handle_v1 *toplevel = calloc(1,
			sizeof(struct wlr_foreign_toplevel_handle_v1));
	if (!toplevel) {
		return NULL;
	}

	wl_list_insert(&manager->toplevels, &toplevel->link);
	toplevel->manager = manager;

	wl_list_init(&toplevel->resources);
	wl_list_init(&toplevel->outputs);

	wl_signal_init(&toplevel->events.request_maximize);
	wl_signal_init(&toplevel->events.request_minimize);
	wl_signal_init(&toplevel->events.request_activate);
	wl_signal_init(&toplevel->events.request_close);
	wl_signal_init(&toplevel->events.set_rectangle);
	wl_signal_init(&toplevel->events.destroy);

	struct wl_resource *manager_resource, *tmp;
	wl_resource_for_each_safe(manager_resource, tmp, &manager->resources) {
		create_toplevel_resource_for_resource(toplevel, manager_resource);
	}

	return toplevel;
}

static const struct zwlr_foreign_toplevel_manager_v1_interface
	foreign_toplevel_manager_impl;

static void foreign_toplevel_manager_handle_stop(struct wl_client *client,
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwlr_foreign_toplevel_manager_v1_interface,
		&foreign_toplevel_manager_impl));

	zwlr_foreign_toplevel_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface
		foreign_toplevel_manager_impl = {
	.stop = foreign_toplevel_manager_handle_stop
};

static void foreign_toplevel_manager_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void toplevel_send_details_to_toplevel_resource(
		struct wlr_foreign_toplevel_handle_v1 *toplevel,
		struct wl_resource *resource) {
	if (toplevel->title) {
		zwlr_foreign_toplevel_handle_v1_send_title(resource,
		    toplevel->title);
	}
	if (toplevel->app_id) {
		zwlr_foreign_toplevel_handle_v1_send_app_id(resource,
		    toplevel->app_id);
	}

	struct wlr_foreign_toplevel_handle_v1_output *output;
	wl_list_for_each(output, &toplevel->outputs, link) {
		send_output_to_resource(resource, output->output, true);
	}

	struct wl_array states;
	wl_array_init(&states);
	bool r = fill_array_from_toplevel_state(&states, toplevel->state);
	if (!r) {
		wl_resource_post_no_memory(resource);
		wl_array_release(&states);
		return;
	}

	zwlr_foreign_toplevel_handle_v1_send_state(resource, &states);
	wl_array_release(&states);

	zwlr_foreign_toplevel_handle_v1_send_done(resource);
}

static void foreign_toplevel_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_foreign_toplevel_manager_v1 *manager = data;
	struct wl_resource *resource = wl_resource_create(client,
			&zwlr_foreign_toplevel_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &foreign_toplevel_manager_impl,
			manager, foreign_toplevel_manager_resource_destroy);

	wl_list_insert(&manager->resources, wl_resource_get_link(resource));

	struct wlr_foreign_toplevel_handle_v1 *toplevel, *tmp;
	wl_list_for_each_safe(toplevel, tmp, &manager->toplevels, link) {
		struct wl_resource *toplevel_resource =
			create_toplevel_resource_for_resource(toplevel, resource);
		toplevel_send_details_to_toplevel_resource(toplevel,
		    toplevel_resource);
	}
}

void wlr_foreign_toplevel_manager_v1_destroy(
		struct wlr_foreign_toplevel_manager_v1 *manager) {
	if (!manager) {
		return;
	}

	struct wlr_foreign_toplevel_handle_v1 *toplevel, *tmp_toplevel;
	wl_list_for_each_safe(toplevel, tmp_toplevel, &manager->toplevels, link) {
		wlr_foreign_toplevel_handle_v1_destroy(toplevel);
	}

	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &manager->resources) {
		wl_resource_destroy(resource);
	}

	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);

	wl_global_destroy(manager->global);
	free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_foreign_toplevel_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_foreign_toplevel_manager_v1_destroy(manager);
}

struct wlr_foreign_toplevel_manager_v1 *wlr_foreign_toplevel_manager_v1_create(
		struct wl_display *display) {
	struct wlr_foreign_toplevel_manager_v1 *manager = calloc(1,
			sizeof(struct wlr_foreign_toplevel_manager_v1));
	if (!manager) {
		return NULL;
	}

	manager->event_loop = wl_display_get_event_loop(display);
	manager->global = wl_global_create(display,
			&zwlr_foreign_toplevel_manager_v1_interface,
			FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION, manager,
			foreign_toplevel_manager_bind);
	if (!manager->global) {
		free(manager);
		return NULL;
	}

	wl_signal_init(&manager->events.destroy);
	wl_list_init(&manager->resources);
	wl_list_init(&manager->toplevels);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
