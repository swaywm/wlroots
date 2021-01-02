#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <wlr/types/wlr_ext_foreign_toplevel_info_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "foreign-toplevel-info-unstable-v1-protocol.h"

#define FOREIGN_TOPLEVEL_INFO_V1_VERSION 1

static const struct zext_foreign_toplevel_handle_v1_interface toplevel_handle_impl;

static void foreign_toplevel_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zext_foreign_toplevel_handle_v1_interface toplevel_handle_impl = {
	.destroy = foreign_toplevel_handle_destroy,
};

static void toplevel_idle_send_done(void *data) {
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel = data;
	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zext_foreign_toplevel_handle_v1_send_done(resource);
	}

	toplevel->idle_source = NULL;
}

static void toplevel_update_idle_source(
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel) {
	if (toplevel->idle_source) {
		return;
	}

	toplevel->idle_source = wl_event_loop_add_idle(toplevel->info->event_loop,
		toplevel_idle_send_done, toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_set_title(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel, const char *title) {
	free(toplevel->title);
	toplevel->title = strdup(title);
	if (toplevel->title == NULL) {
		wlr_log(WLR_ERROR, "failed to allocate memory for toplevel title");
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zext_foreign_toplevel_handle_v1_send_title(resource, title);
	}

	toplevel_update_idle_source(toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_set_app_id(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel, const char *app_id) {
	free(toplevel->app_id);
	toplevel->app_id = strdup(app_id);
	if (toplevel->app_id == NULL) {
		wlr_log(WLR_ERROR, "failed to allocate memory for toplevel app_id");
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		zext_foreign_toplevel_handle_v1_send_app_id(resource, app_id);
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
				zext_foreign_toplevel_handle_v1_send_output_enter(resource,
					output_resource);
			} else {
				zext_foreign_toplevel_handle_v1_send_output_leave(resource,
					output_resource);
			}
		}
	}
}

static void toplevel_send_output(struct wlr_ext_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_output *output, bool enter) {
	struct wl_resource *resource;
	wl_resource_for_each(resource, &toplevel->resources) {
		send_output_to_resource(resource, output, enter);
	}

	toplevel_update_idle_source(toplevel);
}

static void toplevel_handle_output_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_ext_foreign_toplevel_handle_v1_output *toplevel_output =
		wl_container_of(listener, toplevel_output, output_destroy);
	wlr_ext_foreign_toplevel_handle_v1_output_leave(toplevel_output->toplevel,
		toplevel_output->output);
}

void wlr_ext_foreign_toplevel_handle_v1_output_enter(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_output *output) {
	struct wlr_ext_foreign_toplevel_handle_v1_output *toplevel_output;
	wl_list_for_each(toplevel_output, &toplevel->outputs, link) {
		if (toplevel_output->output == output) {
			return; // we have already sent output_enter event
		}
	}

	toplevel_output =
		calloc(1, sizeof(struct wlr_ext_foreign_toplevel_handle_v1_output));
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
		struct wlr_ext_foreign_toplevel_handle_v1_output *toplevel_output) {
	wl_list_remove(&toplevel_output->link);
	wl_list_remove(&toplevel_output->output_destroy.link);
	free(toplevel_output);
}

void wlr_ext_foreign_toplevel_handle_v1_output_leave(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_output *output) {
	struct wlr_ext_foreign_toplevel_handle_v1_output *toplevel_output;

	wl_list_for_each(toplevel_output, &toplevel->outputs, link) {
		if (toplevel_output->output == output) {
			break;
		}
	}

    // The output must be in the list, leaving an output that was never
    // entered is forbidden.
	assert(&toplevel_output->link != &toplevel->outputs);

	toplevel_send_output(toplevel, output, false);
	toplevel_output_destroy(toplevel_output);
}

static bool fill_array_from_toplevel_state(struct wl_array *array,
		uint32_t state) {
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	}
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	}
	if (state & WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) {
		uint32_t *index = wl_array_add(array, sizeof(uint32_t));
		if (index == NULL) {
			return false;
		}
		*index = ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
	}

	return true;
}

static void toplevel_send_state(
        struct wlr_ext_foreign_toplevel_handle_v1 *toplevel) {
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
		zext_foreign_toplevel_handle_v1_send_state(resource, &states);
	}

	wl_array_release(&states);
	toplevel_update_idle_source(toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_set_maximized(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel, bool maximized) {
	if (maximized == (toplevel->state &
	        WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)) {
	    return;
	}
	if (maximized) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	}
	toplevel_send_state(toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_set_minimized(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel, bool minimized) {
	if (minimized == (toplevel->state &
	        WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)) {
	    return;
	}
	if (minimized) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
	toplevel_send_state(toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_set_activated(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel, bool activated) {
	if (activated == (toplevel->state &
	        WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)) {
	    return;
	}
	if (activated) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	}
	toplevel_send_state(toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_set_fullscreen(
		struct wlr_ext_foreign_toplevel_handle_v1 * toplevel, bool fullscreen) {
	if (fullscreen == (toplevel->state &
	        WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN)) {
	    return;
	}
	if (fullscreen) {
		toplevel->state |= WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
	} else {
		toplevel->state &= ~WLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
	}
	toplevel_send_state(toplevel);
}

static void toplevel_resource_send_parent(
		struct wl_resource *toplevel_resource,
		struct wlr_ext_foreign_toplevel_handle_v1 *parent) {
	struct wl_client *client = wl_resource_get_client(toplevel_resource);
	struct wl_resource *parent_resource = NULL;
	if (parent) {
		parent_resource = wl_resource_find_for_client(&parent->resources, client);
		if (!parent_resource) {
			/* don't send an event if this client destroyed the parent handle */
			return;
		}
	}
	zext_foreign_toplevel_handle_v1_send_parent(toplevel_resource,
		parent_resource);
}

void wlr_ext_foreign_toplevel_handle_v1_set_parent(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel,
		struct wlr_ext_foreign_toplevel_handle_v1 *parent) {
	if (parent == toplevel->parent) {
		/* only send parent event to the clients if there was a change */
		return;
	}
	struct wl_resource *toplevel_resource, *tmp;
	wl_resource_for_each_safe(toplevel_resource, tmp, &toplevel->resources) {
		toplevel_resource_send_parent(toplevel_resource, parent);
	}
	toplevel->parent = parent;
	toplevel_update_idle_source(toplevel);
}

void wlr_ext_foreign_toplevel_handle_v1_destroy(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel) {
	if (!toplevel) {
		return;
	}

	wlr_signal_emit_safe(&toplevel->events.destroy, toplevel);

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &toplevel->resources) {
		zext_foreign_toplevel_handle_v1_send_closed(resource);
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}

	struct wlr_ext_foreign_toplevel_handle_v1_output *toplevel_output, *tmp2;
	wl_list_for_each_safe(toplevel_output, tmp2, &toplevel->outputs, link) {
		toplevel_output_destroy(toplevel_output);
	}

	if (toplevel->idle_source) {
		wl_event_source_remove(toplevel->idle_source);
	}

	wl_list_remove(&toplevel->link);

	/* need to ensure no other toplevels hold a pointer to this one as
	 * a parent so that a later call to foreign_toplevel_info_bind()
	 * will not result in a segfault */
	struct wlr_ext_foreign_toplevel_handle_v1 *tl, *tmp3;
	wl_list_for_each_safe(tl, tmp3, &toplevel->info->toplevels, link) {
		if (tl->parent == toplevel) {
			/* Note: we send a parent signal to all clients in this case;
			 * the caller should first destroy the child handles if it
			 * wishes to avoid this behavior. */
			wlr_ext_foreign_toplevel_handle_v1_set_parent(tl, NULL);
		}
	}

	free(toplevel->title);
	free(toplevel->app_id);
	free(toplevel);
}

static void foreign_toplevel_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static struct wl_resource *create_toplevel_resource_for_resource(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel,
		struct wl_resource *info_resource) {
	struct wl_client *client = wl_resource_get_client(info_resource);
	struct wl_resource *resource = wl_resource_create(client,
			&zext_foreign_toplevel_handle_v1_interface,
			wl_resource_get_version(info_resource), 0);
	if (!resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

	wl_resource_set_implementation(resource, &toplevel_handle_impl, toplevel,
		foreign_toplevel_resource_destroy);

	wl_list_insert(&toplevel->resources, wl_resource_get_link(resource));
	zext_foreign_toplevel_info_v1_send_toplevel(info_resource, resource);
	return resource;
}

struct wlr_ext_foreign_toplevel_handle_v1 *
wlr_ext_foreign_toplevel_handle_v1_create(
		struct wlr_ext_foreign_toplevel_info_v1 *info) {
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel = calloc(1,
			sizeof(struct wlr_ext_foreign_toplevel_handle_v1));
	if (!toplevel) {
		return NULL;
	}

	wl_list_insert(&info->toplevels, &toplevel->link);
	toplevel->info = info;

	wl_list_init(&toplevel->resources);
	wl_list_init(&toplevel->outputs);

	wl_signal_init(&toplevel->events.destroy);

	struct wl_resource *info_resource, *tmp;
	wl_resource_for_each_safe(info_resource, tmp, &info->resources) {
		create_toplevel_resource_for_resource(toplevel, info_resource);
	}

	return toplevel;
}

static const struct zext_foreign_toplevel_info_v1_interface
	foreign_toplevel_info_impl;

static void foreign_toplevel_info_handle_stop(struct wl_client *client,
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zext_foreign_toplevel_info_v1_interface,
		&foreign_toplevel_info_impl));

	zext_foreign_toplevel_info_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct zext_foreign_toplevel_info_v1_interface
		foreign_toplevel_info_impl = {
	.stop = foreign_toplevel_info_handle_stop
};

static void foreign_toplevel_info_resource_destroy(
		struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void toplevel_send_details_to_toplevel_resource(
		struct wlr_ext_foreign_toplevel_handle_v1 *toplevel,
		struct wl_resource *resource) {
	if (toplevel->title) {
		zext_foreign_toplevel_handle_v1_send_title(resource, toplevel->title);
	}
	if (toplevel->app_id) {
		zext_foreign_toplevel_handle_v1_send_app_id(resource, toplevel->app_id);
	}

	struct wlr_ext_foreign_toplevel_handle_v1_output *output;
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

	zext_foreign_toplevel_handle_v1_send_state(resource, &states);
	wl_array_release(&states);

	toplevel_resource_send_parent(resource, toplevel->parent);

	zext_foreign_toplevel_handle_v1_send_done(resource);
}

static void foreign_toplevel_info_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_ext_foreign_toplevel_info_v1 *info = data;
	struct wl_resource *resource = wl_resource_create(client,
			&zext_foreign_toplevel_info_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &foreign_toplevel_info_impl,
			info, foreign_toplevel_info_resource_destroy);

	wl_list_insert(&info->resources, wl_resource_get_link(resource));

	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel, *tmp;
	/* First loop: create a handle for all toplevels for all clients.
	 * Separation into two loops avoid the case where a child handle
	 * is created before a parent handle, so the parent relationship
	 * could not be sent to a client. */
	wl_list_for_each_safe(toplevel, tmp, &info->toplevels, link) {
		create_toplevel_resource_for_resource(toplevel, resource);
	}
	/* Second loop: send details about each toplevel. */
	wl_list_for_each_safe(toplevel, tmp, &info->toplevels, link) {
		struct wl_resource *toplevel_resource =
			wl_resource_find_for_client(&toplevel->resources, client);
		toplevel_send_details_to_toplevel_resource(toplevel,
			toplevel_resource);
	}
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_ext_foreign_toplevel_info_v1 *info =
		wl_container_of(listener, info, display_destroy);
	wlr_signal_emit_safe(&info->events.destroy, info);
	wl_list_remove(&info->display_destroy.link);
	wl_global_destroy(info->global);
	free(info);
}

struct wlr_ext_foreign_toplevel_info_v1 *wlr_ext_foreign_toplevel_info_v1_create(
		struct wl_display *display) {
	struct wlr_ext_foreign_toplevel_info_v1 *info = calloc(1,
			sizeof(struct wlr_ext_foreign_toplevel_info_v1));
	if (!info) {
		return NULL;
	}

	info->event_loop = wl_display_get_event_loop(display);
	info->global = wl_global_create(display,
			&zext_foreign_toplevel_info_v1_interface,
			FOREIGN_TOPLEVEL_INFO_V1_VERSION, info,
			foreign_toplevel_info_bind);
	if (!info->global) {
		free(info);
		return NULL;
	}

	wl_signal_init(&info->events.destroy);
	wl_list_init(&info->resources);
	wl_list_init(&info->toplevels);

	info->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &info->display_destroy);

	return info;
}
