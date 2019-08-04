#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <util/signal.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/types/wlr_input_timestamps_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "input-timestamps-unstable-v1-protocol.h"

#define INPUT_TIMESTAMPS_MANAGER_VERSION 1

static const struct zwp_input_timestamps_manager_v1_interface manager_impl;
static const struct zwp_input_timestamps_v1_interface impl;

/**
 * helper functions
 */

static struct wlr_input_timestamps_v1 *
from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_timestamps_v1_interface,
		&impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_input_timestamps_manager_v1 *
manager_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_timestamps_manager_v1_interface,
		&manager_impl));
	return wl_resource_get_user_data(resource);
}

/**
 * input_timestamps handler functions
 */

static void destroy(struct wlr_input_timestamps_v1 *input_timestamps) {
	wlr_signal_emit_safe(&input_timestamps->events.destroy, input_timestamps);

	wl_list_remove(&input_timestamps->link);
	wl_list_remove(&input_timestamps->seat_destroy.link);
	wl_list_remove(&input_timestamps->input_destroy.link);

	wl_resource_set_user_data(input_timestamps->resource, NULL);
	free(input_timestamps);
}

static void handle_resource_destroy(struct wl_resource *resource) {
	struct wlr_input_timestamps_v1 *input_timestamps =
		from_resource(resource);
	if (input_timestamps == NULL) {
		return;
	}

	destroy(input_timestamps);
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	struct wlr_input_timestamps_v1 *input_timestamps =
		from_resource(resource);
	wlr_log(WLR_DEBUG, "input_timestamps_v1 %p released by client %p",
		input_timestamps, client);

	wl_resource_destroy(resource);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_timestamps_v1 *input_timestamps =
		wl_container_of(listener, input_timestamps, seat_destroy);

	destroy(input_timestamps);
}

static void handle_input_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_timestamps_v1 *input_timestamps =
		wl_container_of(listener, input_timestamps, input_destroy);

	destroy(input_timestamps);
}

/**
 * input_timestamps_manager handler functions
 */

static void manager_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);

	wlr_log(WLR_DEBUG,
		"input_timestamps_v1 manager unbound from client %p", client);
}

static void manager_handle_get_input_timestamps(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *input,
		enum wlr_input_timestamps_type input_timestamps_type) {
	struct wlr_input_timestamps_manager_v1 *manager =
		manager_from_resource(resource);
	assert(manager);

	struct wlr_seat_client *seat_client = NULL;
	switch (input_timestamps_type) {
	case WLR_INPUT_TIMESTAMPS_KEYBOARD:
		seat_client = wlr_seat_client_from_keyboard_resource(input);
		break;
	case WLR_INPUT_TIMESTAMPS_POINTER:
		seat_client = wlr_seat_client_from_pointer_resource(input);
		break;
	case WLR_INPUT_TIMESTAMPS_TOUCH:
		seat_client = wlr_seat_client_from_touch_resource(input);
		break;
	}

	struct wlr_input_timestamps_v1 *input_timestamps =
		calloc(1, sizeof(struct wlr_input_timestamps_v1));
	if (input_timestamps == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *input_timestamps_resource = wl_resource_create(client,
		&zwp_input_timestamps_v1_interface, wl_resource_get_version(resource), id);
	if (input_timestamps_resource == NULL) {
		free(input_timestamps);
		wl_client_post_no_memory(client);
		return;
	}

	input_timestamps->resource = input_timestamps_resource;
	input_timestamps->input_resource = input;
	input_timestamps->seat = seat_client->seat;

	wl_list_insert(&manager->input_timestamps, &input_timestamps->link);

	input_timestamps->input_timestamps_type = input_timestamps_type;

	wl_signal_init(&input_timestamps->events.destroy);

	wl_signal_add(&input_timestamps->seat->events.destroy,
			&input_timestamps->seat_destroy);
	input_timestamps->seat_destroy.notify = handle_seat_destroy;

	wl_resource_add_destroy_listener(input_timestamps->input_resource,
			&input_timestamps->input_destroy);
	input_timestamps->input_destroy.notify = handle_input_destroy;

	wl_resource_set_implementation(input_timestamps_resource, &impl,
		input_timestamps, handle_resource_destroy);

	switch (input_timestamps_type) {
	case WLR_INPUT_TIMESTAMPS_KEYBOARD:
		wlr_log(WLR_DEBUG,
			"input_timestamps_v1 %p created for client %p for keyboard events",
			input_timestamps, client);
		break;
	case WLR_INPUT_TIMESTAMPS_POINTER:
		wlr_log(WLR_DEBUG,
			"input_timestamps_v1 %p created for client %p for pointer events",
			input_timestamps, client);
		break;
	case WLR_INPUT_TIMESTAMPS_TOUCH:
		wlr_log(WLR_DEBUG,
			"input_timestamps_v1 %p created for client %p for touch events",
			input_timestamps, client);
		break;
	}
}

static void manager_handle_get_keyboard_timestamps(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *keyboard) {
	manager_handle_get_input_timestamps(client, resource, id, keyboard,
		WLR_INPUT_TIMESTAMPS_KEYBOARD);
}

static void manager_handle_get_pointer_timestamps(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *pointer) {
	manager_handle_get_input_timestamps(client, resource, id, pointer,
		WLR_INPUT_TIMESTAMPS_POINTER);
}

static void manager_handle_get_touch_timestamps(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, struct wl_resource *touch) {
	manager_handle_get_input_timestamps(client, resource, id, touch,
		WLR_INPUT_TIMESTAMPS_TOUCH);
}

static void manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_input_timestamps_manager_v1 *manager = data;

	struct wl_resource *manager_resource = wl_resource_create(wl_client,
		&zwp_input_timestamps_manager_v1_interface, version, id);

	if (manager_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_insert(&manager->resources, wl_resource_get_link(manager_resource));

	wl_resource_set_implementation(manager_resource, &manager_impl,
		manager, manager_handle_resource_destroy);

	wlr_log(WLR_DEBUG, "input_timestamps_v1 manager bound to client %p",
		wl_client);
}

static void manager_handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_timestamps_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy_listener);
	wlr_input_timestamps_manager_v1_destroy(manager);
}

/**
 * Implementations
 */

static const struct zwp_input_timestamps_manager_v1_interface manager_impl = {
	.destroy = manager_handle_destroy,
	.get_keyboard_timestamps = manager_handle_get_keyboard_timestamps,
	.get_pointer_timestamps = manager_handle_get_pointer_timestamps,
	.get_touch_timestamps = manager_handle_get_touch_timestamps,
};

static const struct zwp_input_timestamps_v1_interface impl = {
	.destroy = handle_destroy,
};

/**
 * Public functions
 */

struct wlr_input_timestamps_manager_v1 *wlr_input_timestamps_manager_v1_create(struct wl_display *display) {
	struct wlr_input_timestamps_manager_v1 *manager =
		calloc(1, sizeof(struct wlr_input_timestamps_manager_v1));

	if (manager == NULL) {
		return NULL;
	}

	wl_list_init(&manager->resources);
	wl_list_init(&manager->input_timestamps);

	manager->global = wl_global_create(display,
		&zwp_input_timestamps_manager_v1_interface, INPUT_TIMESTAMPS_MANAGER_VERSION,
		manager, manager_bind);

	if (manager->global == NULL) {
		free(manager);
		return NULL;
	}

	wl_signal_init(&manager->events.destroy);

	manager->display_destroy_listener.notify = manager_handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy_listener);

	wlr_log(WLR_DEBUG, "input_timestamps_v1 manager created");

	return manager;
}

void wlr_input_timestamps_manager_v1_destroy(struct wlr_input_timestamps_manager_v1 *manager) {
	if (manager == NULL) {
		return;
	}

	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy_listener.link);

	struct wlr_input_timestamps_v1 *object, *tmp_object;
	wl_list_for_each_safe(object, tmp_object, &manager->input_timestamps, link) {
		wl_resource_destroy(object->resource);
	}

	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &manager->resources) {
		wl_resource_destroy(resource);
	}

	wl_global_destroy(manager->global);
	free(manager);
}

/**
 * Sends input event timestamps in nanosecond accuracy depending on
 * input_timestamps object type.
 */
static void manager_send_timestamp(struct wlr_input_timestamps_manager_v1 *manager,
		struct wlr_seat *seat, uint64_t tv_sec, uint32_t tv_nsec,
		enum wlr_input_timestamps_type input_timestamps_type) {
	struct wlr_seat_client *focused_client = NULL;
	switch (input_timestamps_type) {
	case WLR_INPUT_TIMESTAMPS_KEYBOARD:
		focused_client = seat->keyboard_state.focused_client;
		break;
	case WLR_INPUT_TIMESTAMPS_POINTER:
		focused_client = seat->pointer_state.focused_client;
		break;
	case WLR_INPUT_TIMESTAMPS_TOUCH:
		// TODO: add support for touch input events
		focused_client = NULL;
		break;
	}
	if (focused_client == NULL) {
		return;
	}

	struct wlr_seat_client *seat_client = NULL;
	struct wlr_input_timestamps_v1 *input_timestamps;
	wl_list_for_each(input_timestamps, &manager->input_timestamps, link) {
		if (input_timestamps_type != input_timestamps->input_timestamps_type) {
			continue;
		}

		switch (input_timestamps_type) {
		case WLR_INPUT_TIMESTAMPS_KEYBOARD:
			seat_client = wlr_seat_client_from_keyboard_resource(
				input_timestamps->input_resource);
			break;
		case WLR_INPUT_TIMESTAMPS_POINTER:
			seat_client = wlr_seat_client_from_pointer_resource(
				input_timestamps->input_resource);
			break;
		case WLR_INPUT_TIMESTAMPS_TOUCH:
			seat_client = wlr_seat_client_from_touch_resource(
				input_timestamps->input_resource);
			break;
		}
		if (seat != input_timestamps->seat || focused_client != seat_client) {
			continue;
		}

		zwp_input_timestamps_v1_send_timestamp(input_timestamps->resource,
			(uint32_t)(tv_sec >> 32), (uint32_t)tv_sec, (uint32_t)tv_nsec);
	}
}

void wlr_input_timestamps_manager_v1_send_keyboard_timestamp(
		struct wlr_input_timestamps_manager_v1 *manager, struct wlr_seat *seat,
		uint64_t tv_sec, uint32_t tv_nsec) {
	manager_send_timestamp(manager, seat, tv_sec, tv_nsec,
		WLR_INPUT_TIMESTAMPS_KEYBOARD);
}

void wlr_input_timestamps_manager_v1_send_pointer_timestamp(
		struct wlr_input_timestamps_manager_v1 *manager, struct wlr_seat *seat,
		uint64_t tv_sec, uint32_t tv_nsec) {
	manager_send_timestamp(manager, seat, tv_sec, tv_nsec,
		WLR_INPUT_TIMESTAMPS_POINTER);
}

void wlr_input_timestamps_manager_v1_send_touch_timestamp(
		struct wlr_input_timestamps_manager_v1 *manager, struct wlr_seat *seat,
		uint64_t tv_sec, uint32_t tv_nsec) {
	manager_send_timestamp(manager, seat, tv_sec, tv_nsec,
		WLR_INPUT_TIMESTAMPS_TOUCH);
}
