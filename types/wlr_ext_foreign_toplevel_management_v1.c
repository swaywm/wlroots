#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <wlr/types/wlr_ext_foreign_toplevel_info_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "util/signal.h"
#include "foreign-toplevel-management-unstable-v1-protocol.h"

#define EXT_FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION 1

static const struct zext_foreign_toplevel_manager_v1_interface manager_impl;

static struct wlr_ext_foreign_toplevel_manager_v1 *manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
			&zext_foreign_toplevel_manager_v1_interface,
			&manager_impl));
	return wl_resource_get_user_data(resource);
}

static void manager_emit_maximized_signal(struct wl_resource *resource,
		struct wl_resource *toplevel_resource, bool state) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		manager_from_resource(resource);
	if (!manager) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel =
		wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel_resource);
	if (!toplevel) {
		return;
	}

	struct wlr_ext_foreign_toplevel_manager_v1_maximize_event event = {
		.manager = manager,
		.toplevel = toplevel,
		.maximize = state,
	};
	wlr_signal_emit_safe(&manager->events.request_maximize, &event);
}

static void manager_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *toplevel_resource) {
	manager_emit_maximized_signal(resource, toplevel_resource, true);
}

static void manager_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *toplevel_resource) {
	manager_emit_maximized_signal(resource, toplevel_resource, false);
}

static void manager_emit_minimized_signal(struct wl_resource *resource,
		struct wl_resource *toplevel_resource, bool state) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		manager_from_resource(resource);
	if (!manager) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel =
		wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel_resource);
	if (!toplevel) {
		return;
	}

	struct wlr_ext_foreign_toplevel_manager_v1_minimize_event event = {
		.manager = manager,
		.toplevel = toplevel,
		.minimize = state,
	};
	wlr_signal_emit_safe(&manager->events.request_minimize, &event);
}

static void manager_handle_set_minimized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *toplevel_resource) {
	manager_emit_minimized_signal(resource, toplevel_resource, true);
}

static void manager_handle_unset_minimized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *toplevel_resource) {
	manager_emit_minimized_signal(resource, toplevel_resource, false);
}

static void manager_emit_fullscreen_signal(struct wl_resource *resource,
		struct wl_resource *toplevel_resource, bool state,
		struct wl_resource *output_resource) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		manager_from_resource(resource);
	if (!manager) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel =
		wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel_resource);
	if (!toplevel) {
		return;
	}

	struct wlr_output *output = NULL;
	if (output_resource) {
		output = wlr_output_from_resource(output_resource);
	}
	struct wlr_ext_foreign_toplevel_manager_v1_fullscreen_event event = {
		.manager = manager,
		.toplevel = toplevel,
		.fullscreen = state,
		.output = output,
	};
	wlr_signal_emit_safe(&manager->events.request_fullscreen, &event);
}

static void manager_handle_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *toplevel_resource,
		struct wl_resource *output) {
	manager_emit_fullscreen_signal(resource, toplevel_resource, true, output);
}

static void manager_handle_unset_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *toplevel_resource) {
	manager_emit_fullscreen_signal(resource, toplevel_resource, false, NULL);
}

static void manager_handle_activate(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *toplevel_resource,
		struct wl_resource *seat_resource) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		manager_from_resource(resource);
	if (!manager) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel =
		wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel_resource);
	if (!toplevel) {
		return;
	}

	struct wlr_seat_client *seat_client =
		wlr_seat_client_from_resource(seat_resource);
	if (!seat_client) {
		return;
	}
	struct wlr_ext_foreign_toplevel_manager_v1_activate_event event = {
		.manager = manager,
		.toplevel = toplevel,
		.seat = seat_client->seat,
	};
	wlr_signal_emit_safe(&manager->events.request_activate, &event);
}

static void manager_handle_close(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *toplevel_resource) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		manager_from_resource(resource);
	if (!manager) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel =
		wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel_resource);
	if (!toplevel) {
		return;
	}

	struct wlr_ext_foreign_toplevel_manager_v1_close_event event = {
	    .manager = manager,
	    .toplevel = toplevel,
	};
	wlr_signal_emit_safe(&manager->events.request_close, &event);
}

static void manager_handle_set_rectangle(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *toplevel_resource,
		struct wl_resource *surface_resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		manager_from_resource(resource);
	if (!manager) {
		return;
	}
	struct wlr_ext_foreign_toplevel_handle_v1 *toplevel =
		wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel_resource);
	if (!toplevel) {
		return;
	}

	if (width < 0 || height < 0) {
		wl_resource_post_error(resource,
			ZEXT_FOREIGN_TOPLEVEL_MANAGER_V1_ERROR_INVALID_RECTANGLE,
			"invalid rectangle passed to set_rectangle: width or height < 0");
		return;
	}

	struct wlr_ext_foreign_toplevel_manager_v1_set_rectangle_event event = {
		.manager = manager,
		.toplevel = toplevel,
		.surface = wlr_surface_from_resource(surface_resource),
		.x = x,
		.y = y,
		.width = width,
		.height = height,
	};
	wlr_signal_emit_safe(&manager->events.set_rectangle, &event);
}

static void manager_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zext_foreign_toplevel_manager_v1_interface manager_impl = {
	.set_maximized = manager_handle_set_maximized,
	.unset_maximized = manager_handle_unset_maximized,
	.set_minimized = manager_handle_set_minimized,
	.unset_minimized = manager_handle_unset_minimized,
	.activate = manager_handle_activate,
	.close = manager_handle_close,
	.set_rectangle = manager_handle_set_rectangle,
	.set_fullscreen = manager_handle_set_fullscreen,
	.unset_fullscreen = manager_handle_unset_fullscreen,
	.destroy = manager_handle_destroy,
};

static void manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager = data;
	struct wl_resource *resource = wl_resource_create(client,
			&zext_foreign_toplevel_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_ext_foreign_toplevel_manager_v1 *wlr_ext_foreign_toplevel_manager_v1_create(
		struct wl_display *display) {
	struct wlr_ext_foreign_toplevel_manager_v1 *manager = calloc(1,
			sizeof(struct wlr_ext_foreign_toplevel_manager_v1));
	if (!manager) {
		return NULL;
	}

	manager->global = wl_global_create(display,
			&zext_foreign_toplevel_manager_v1_interface,
			EXT_FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION, manager,
			manager_bind);
	if (!manager->global) {
		free(manager);
		return NULL;
	}

	wl_signal_init(&manager->events.request_maximize);
	wl_signal_init(&manager->events.request_minimize);
	wl_signal_init(&manager->events.request_activate);
	wl_signal_init(&manager->events.request_fullscreen);
	wl_signal_init(&manager->events.request_close);
	wl_signal_init(&manager->events.set_rectangle);
	wl_signal_init(&manager->events.destroy);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
