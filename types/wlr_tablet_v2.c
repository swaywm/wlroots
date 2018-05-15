#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/util/log.h>
#include "tablet-unstable-v2-protocol.h"

struct wlr_tablet_seat_v2 {
	struct wl_list link;
	struct wlr_seat *wlr_seat;
	struct wlr_tablet_manager_v2 *manager;

	struct wl_list tablets; // wlr_tablet_v2_tablet::link
	struct wl_list tools;
	struct wl_list pads;

	struct wl_list clients; //wlr_tablet_seat_v2_client::link;

	struct wl_listener seat_destroy;
};

struct wlr_tablet_manager_client_v2 {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_tablet_manager_v2 *manager;

	struct wl_listener client_destroy;
	struct wl_list tablet_seats; // wlr_tablet_seat_client_v2::link
};

struct wlr_tablet_seat_client_v2 {
	struct wl_list seat_link;
	struct wl_list client_link;
	struct wl_client *wl_client;
	struct wl_resource *resource;

	struct wlr_tablet_manager_client_v2 *client;
	struct wlr_seat_client *seat;

	struct wl_listener seat_client_destroy;

	struct wl_list tools;   //wlr_tablet_tool_client_v2::link
	struct wl_list tablets; //wlr_tablet_client_v2::link
	struct wl_list pads;    //wlr_tablet_pad_client_v2::link
};

struct wlr_tablet_client_v2 {
	struct wl_list seat_link; // wlr_tablet_seat_client_v2::tablet
	struct wl_list tablet_link; // wlr_tablet_v2_tablet::clients
	struct wl_client *client;
	struct wl_resource *resource;
};

struct wlr_tablet_tool_client_v2 {
	struct wl_list seat_link;
	struct wl_list tool_link;
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_tablet_v2_tablet_tool *tool;

	uint32_t proximity_serial;

	struct wl_event_source *frame_source;
};

struct wlr_tablet_pad_client_v2 {
	struct wl_list seat_link;
	struct wl_list pad_link;
	struct wl_client *client;
	struct wl_resource *resource;
	struct wlr_tablet_v2_tablet_pad *pad;

	uint32_t enter_serial;
	uint32_t mode_serial;
	uint32_t leave_serial;

	size_t button_count;

	size_t group_count;
	struct wl_resource **groups;

	size_t ring_count;
	struct wl_resource **rings;

	size_t strip_count;
	struct wl_resource **strips;
};

struct tablet_pad_auxiliary_user_data {
	struct wlr_tablet_pad_client_v2 *pad;
	size_t index;
};

static struct zwp_tablet_v2_interface tablet_impl;

static struct wlr_tablet_client_v2 *tablet_client_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_v2_interface,
		&tablet_impl));
	return wl_resource_get_user_data(resource);
}

static void destroy_tablet_v2(struct wl_resource *resource) {
	struct wlr_tablet_client_v2 *tablet = tablet_client_from_resource(resource);

	wl_list_remove(&tablet->seat_link);
	wl_list_remove(&tablet->tablet_link);
	free(tablet);
}

static void handle_tablet_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_v2_interface tablet_impl = {
	.destroy = handle_tablet_v2_destroy,
};

static void destroy_tablet_seat_client(struct wlr_tablet_seat_client_v2 *client) {
	/* This is only called when the seat or client gets destroyed.
	 * The client is liable to make a request on a deleted resource either
	 * way, so we don't do the removed->destroy process, but just remove
	 * all structs immediatly.
	 */
	struct wlr_tablet_client_v2 *tablet;
	struct wlr_tablet_client_v2 *tmp_tablet;
	wl_list_for_each_safe(tablet, tmp_tablet, &client->tablets, seat_link) {
		wl_resource_destroy(tablet->resource);
	}

	struct wlr_tablet_pad_client_v2 *pad;
	struct wlr_tablet_pad_client_v2 *tmp_pad;
	wl_list_for_each_safe(pad, tmp_pad, &client->pads, seat_link) {
		wl_resource_destroy(pad->resource);
	}

	struct wlr_tablet_tool_client_v2 *tool;
	struct wlr_tablet_tool_client_v2 *tmp_tool;
	wl_list_for_each_safe(tool, tmp_tool, &client->tools, seat_link) {
		wl_resource_destroy(tool->resource);
	}

	wl_resource_destroy(client->resource);
}

static void handle_wlr_seat_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_seat_v2 *seat =
		wl_container_of(listener, seat, seat_destroy);

	wl_list_remove(&seat->link);
	wl_list_remove(&seat->seat_destroy.link);

	struct wlr_tablet_seat_client_v2 *client;
	struct wlr_tablet_seat_client_v2 *tmp;

	/* wl_seat doesn't have a removed event/destroy request, so we can just
	 * destroy all attached tablet_seat_clients -> tablet_v2 resources.
	 * The client can call requests on gone resources either way
	 */
	wl_list_for_each_safe(client, tmp, &seat->clients, seat_link) {
		destroy_tablet_seat_client(client);
	}
}

static struct wlr_tablet_seat_v2 *make_tablet_seat(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat) {
	struct wlr_tablet_seat_v2 *tablet_seat =
		calloc(1, sizeof(struct wlr_tablet_seat_v2));
	if (!tablet_seat) {
		return NULL;
	}

	tablet_seat->manager = manager;
	tablet_seat->wlr_seat = wlr_seat;

	wl_list_init(&tablet_seat->clients);

	wl_list_init(&tablet_seat->tablets);
	wl_list_init(&tablet_seat->tools);
	wl_list_init(&tablet_seat->pads);

	tablet_seat->seat_destroy.notify = handle_wlr_seat_destroy;
	wl_signal_add(&wlr_seat->events.destroy, &tablet_seat->seat_destroy);

	wl_list_insert(&manager->seats, &tablet_seat->link);
	return tablet_seat;
}

static struct wlr_tablet_seat_v2 *get_or_make_tablet_seat(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat) {
	struct wlr_tablet_seat_v2 *pos;
	wl_list_for_each(pos, &manager->seats, link) {
		if (pos->wlr_seat == wlr_seat) {
			return pos;
		}
	}

	return make_tablet_seat(manager, wlr_seat);
}

static void add_tablet_client(struct wlr_tablet_seat_client_v2 *seat,
		struct wlr_tablet_v2_tablet *tablet) {
	struct wlr_tablet_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_client_v2));
	if (!client) {
		return;
	}

	client->resource =
		wl_resource_create(seat->wl_client, &zwp_tablet_v2_interface, 1, 0);
	if (!client->resource) {
		free(client);
		return;
	}
	wl_resource_set_implementation(client->resource, &tablet_impl,
		client, destroy_tablet_v2);
	zwp_tablet_seat_v2_send_tablet_added(seat->resource, client->resource);

	// Send the expected events
	if (tablet->wlr_tool->name) {
		zwp_tablet_v2_send_name(client->resource, tablet->wlr_tool->name);
	}
	zwp_tablet_v2_send_id(client->resource,
		tablet->wlr_device->vendor, tablet->wlr_device->product);
	for (size_t i = 0; i < tablet->wlr_tool->paths.length; ++i) {
		zwp_tablet_v2_send_path(client->resource,
			tablet->wlr_tool->paths.items[i]);
	}
	zwp_tablet_v2_send_done(client->resource);

	client->client = seat->wl_client;
	wl_list_insert(&seat->tablets, &client->seat_link);
	wl_list_insert(&tablet->clients, &client->tablet_link);
}

static void handle_wlr_tablet_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_v2_tablet *tablet =
		wl_container_of(listener, tablet, tool_destroy);

	struct wlr_tablet_client_v2 *pos;
	struct wlr_tablet_client_v2 *tmp;
	wl_list_for_each_safe(pos, tmp, &tablet->clients, tablet_link) {
		// XXX: Add a timer/flag to destroy if client is slow?
		zwp_tablet_v2_send_removed(pos->resource);
	}

	wl_list_remove(&tablet->clients);
	wl_list_remove(&tablet->link);
	wl_list_remove(&tablet->tool_destroy.link);
	free(tablet);
}

static void handle_tablet_tool_v2_set_cursor(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t serial,
		struct wl_resource *surface_resource,
		int32_t hotspot_x,
		int32_t hotspot_y) {
	struct wlr_tablet_tool_client_v2 *tool = wl_resource_get_user_data(resource);
	if (!tool) {
		return;
	}

	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	struct wlr_tablet_v2_event_cursor evt = {
		.surface = surface,
		.serial = serial,
		.hotspot_x = hotspot_x,
		.hotspot_y = hotspot_y,
		};

	wl_signal_emit(&tool->tool->events.set_cursor, &evt);
}

static void handle_tablet_tool_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}
static struct zwp_tablet_tool_v2_interface tablet_tool_impl = {
	.set_cursor = handle_tablet_tool_v2_set_cursor,
	.destroy = handle_tablet_tool_v2_destroy,
};

static enum zwp_tablet_tool_v2_type tablet_type_from_wlr_type(
		enum wlr_tablet_tool_type wlr_type) {
	switch(wlr_type) {
	case WLR_TABLET_TOOL_TYPE_PEN:
		return ZWP_TABLET_TOOL_V2_TYPE_PEN;
	case WLR_TABLET_TOOL_TYPE_ERASER:
		return ZWP_TABLET_TOOL_V2_TYPE_ERASER;
	case WLR_TABLET_TOOL_TYPE_BRUSH:
		return ZWP_TABLET_TOOL_V2_TYPE_BRUSH;
	case WLR_TABLET_TOOL_TYPE_PENCIL:
		return ZWP_TABLET_TOOL_V2_TYPE_PENCIL;
	case WLR_TABLET_TOOL_TYPE_AIRBRUSH:
		return ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH;
	case WLR_TABLET_TOOL_TYPE_MOUSE:
		return ZWP_TABLET_TOOL_V2_TYPE_MOUSE;
	case WLR_TABLET_TOOL_TYPE_LENS:
		return ZWP_TABLET_TOOL_V2_TYPE_LENS;
	}

	assert(false && "Unreachable");
}

static void destroy_tablet_tool(struct wl_resource *resource) {
	struct wlr_tablet_tool_client_v2 *client =
		wl_resource_get_user_data(resource);

	if (client->frame_source) {
		wl_event_source_remove(client->frame_source);
	}

	wl_list_remove(&client->seat_link);
	wl_list_remove(&client->tool_link);
	free(client);
}

static void add_tablet_tool_client(struct wlr_tablet_seat_client_v2 *seat,
		struct wlr_tablet_v2_tablet_tool *tool) {
	struct wlr_tablet_tool_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_tool_client_v2));
	if (!client) {
		return;
	}
	client->tool = tool;

	client->resource =
		wl_resource_create(seat->wl_client, &zwp_tablet_tool_v2_interface, 1, 0);
	if (!client->resource) {
		free(client);
		return;
	}
	wl_resource_set_implementation(client->resource, &tablet_tool_impl,
		client, destroy_tablet_tool);
	zwp_tablet_seat_v2_send_tool_added(seat->resource, client->resource);

	// Send the expected events
	if (tool->wlr_tool->hardware_serial) {
			zwp_tablet_tool_v2_send_hardware_serial(
			client->resource, 
			tool->wlr_tool->hardware_serial >> 32,
			tool->wlr_tool->hardware_serial & 0xFFFFFFFF);
	}
	if (tool->wlr_tool->hardware_wacom) {
			zwp_tablet_tool_v2_send_hardware_id_wacom(
			client->resource, 
			tool->wlr_tool->hardware_wacom >> 32,
			tool->wlr_tool->hardware_wacom & 0xFFFFFFFF);
	}
	zwp_tablet_tool_v2_send_type(client->resource, 
		tablet_type_from_wlr_type(tool->wlr_tool->type));

	if (tool->wlr_tool->tilt) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_TILT);
	}

	if (tool->wlr_tool->pressure) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE);
	}

	if (tool->wlr_tool->distance) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_DISTANCE);
	}

	if (tool->wlr_tool->rotation) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_ROTATION);
	}

	if (tool->wlr_tool->slider) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_SLIDER);
	}

	if (tool->wlr_tool->wheel) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_WHEEL);
	}

	zwp_tablet_tool_v2_send_done(client->resource);

	client->client = seat->wl_client;
	wl_list_insert(&seat->tools, &client->seat_link);
	wl_list_insert(&tool->clients, &client->tool_link);
}

struct wlr_tablet_v2_tablet *wlr_tablet_create(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_input_device *wlr_device) {
	assert(wlr_device->type == WLR_INPUT_DEVICE_TABLET_TOOL);
	struct wlr_tablet_seat_v2 *seat = get_or_make_tablet_seat(manager, wlr_seat);
	if (!seat) {
		return NULL;
	}
	struct wlr_tablet_tool *tool = wlr_device->tablet_tool;
	struct wlr_tablet_v2_tablet *tablet = calloc(1, sizeof(struct wlr_tablet_v2_tablet));
	if (!tablet) {
		return NULL;
	}

	tablet->wlr_tool = tool;
	tablet->wlr_device = wlr_device;
	wl_list_init(&tablet->clients);


	tablet->tool_destroy.notify = handle_wlr_tablet_destroy;
	wl_signal_add(&wlr_device->events.destroy, &tablet->tool_destroy);
	wl_list_insert(&seat->tablets, &tablet->link);

	// We need to create a tablet client for all clients on the seat
	struct wlr_tablet_seat_client_v2 *pos;
	wl_list_for_each(pos, &seat->clients, seat_link) {
		// Tell the clients about the new tool
		add_tablet_client(pos, tablet);
	}

	return tablet;
}

static void handle_wlr_tablet_tool_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_v2_tablet_tool *tool =
		wl_container_of(listener, tool, tool_destroy);

	struct wlr_tablet_tool_client_v2 *pos;
	struct wlr_tablet_tool_client_v2 *tmp;
	wl_list_for_each_safe(pos, tmp, &tool->clients, tool_link) {
		// XXX: Add a timer/flag to destroy if client is slow?
		zwp_tablet_tool_v2_send_removed(pos->resource);
	}

	wl_list_remove(&tool->clients);
	wl_list_remove(&tool->link);
	wl_list_remove(&tool->tool_destroy.link);
	wl_list_remove(&tool->events.set_cursor.listener_list);
	free(tool);
}

struct wlr_tablet_v2_tablet_tool *wlr_tablet_tool_create(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_tablet_tool_tool *wlr_tool) {
	struct wlr_tablet_seat_v2 *seat = get_or_make_tablet_seat(manager, wlr_seat);
	if (!seat) {
		return NULL;
	}
	struct wlr_tablet_v2_tablet_tool *tool =
		calloc(1, sizeof(struct wlr_tablet_v2_tablet_tool));
	if (!tool) {
		return NULL;
	}

	tool->wlr_tool = wlr_tool;
	wl_list_init(&tool->clients);


	tool->tool_destroy.notify = handle_wlr_tablet_tool_destroy;
	wl_signal_add(&wlr_tool->events.destroy, &tool->tool_destroy);
	wl_list_insert(&seat->tools, &tool->link);

	// We need to create a tablet client for all clients on the seat
	struct wlr_tablet_seat_client_v2 *pos;
	wl_list_for_each(pos, &seat->clients, seat_link) {
		// Tell the clients about the new tool
		add_tablet_tool_client(pos, tool);
	}

	wl_signal_init(&tool->events.set_cursor);

	return tool;
}

static struct zwp_tablet_pad_v2_interface tablet_pad_impl;

static struct wlr_tablet_pad_client_v2 *tablet_pad_client_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_pad_v2_interface,
		&tablet_pad_impl));
	return wl_resource_get_user_data(resource);
}


static void destroy_tablet_pad_v2(struct wl_resource *resource) {
	struct wlr_tablet_pad_client_v2 *pad =
		tablet_pad_client_from_resource(resource);

	wl_list_remove(&pad->seat_link);
	wl_list_remove(&pad->pad_link);

	/* This isn't optimal, if the client destroys the resources in another
	 * order, it will be disconnected.
	 * But this makes things *way* easier for us, and (untested) I doubt
	 * clients will destroy it in another order.
	 */
	for (size_t i = 0; i < pad->group_count; ++i) {
		if (pad->groups[i]) {
			wl_resource_destroy(pad->groups[i]);
		}
	}
	free(pad->groups);

	for (size_t i = 0; i < pad->ring_count; ++i) {
		if (pad->rings[i]) {
			wl_resource_destroy(pad->rings[i]);
		}
	}
	free(pad->rings);

	for (size_t i = 0; i < pad->strip_count; ++i) {
		if (pad->strips[i]) {
			wl_resource_destroy(pad->strips[i]);
		}
	}
	free(pad->strips);

	free(pad);
}

static void handle_tablet_pad_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void destroy_tablet_pad_ring_v2(struct wl_resource *resource) {
	struct tablet_pad_auxiliary_user_data *aux = wl_resource_get_user_data(resource);

	if (!aux) {
		return;
	}

	aux->pad->rings[aux->index] = NULL;
	free(aux);
	wl_resource_set_user_data(resource, NULL);
}
static void handle_tablet_pad_ring_v2_set_feedback(struct wl_client *client,
		struct wl_resource *resource, const char *description,
		uint32_t serial) {
	struct tablet_pad_auxiliary_user_data *aux = wl_resource_get_user_data(resource);
	if (!aux) {
		return;
	}

	struct wlr_tablet_v2_event_feedback evt = {
		.serial = serial,
		.description = description,
		.index = aux->index
		};

	wl_signal_emit(&aux->pad->pad->events.ring_feedback, &evt);
}

static void handle_tablet_pad_ring_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_pad_ring_v2_interface tablet_pad_ring_impl = {
	.set_feedback = handle_tablet_pad_ring_v2_set_feedback,
	.destroy = handle_tablet_pad_ring_v2_destroy,
};

static void destroy_tablet_pad_strip_v2(struct wl_resource *resource) {
	struct tablet_pad_auxiliary_user_data *aux = wl_resource_get_user_data(resource);
	if (!aux) {
		return;
	}

	aux->pad->strips[aux->index] = NULL;
	free(aux);
	wl_resource_set_user_data(resource, NULL);
}

static void handle_tablet_pad_strip_v2_set_feedback(struct wl_client *client,
		struct wl_resource *resource, const char *description,
		uint32_t serial) {
	struct tablet_pad_auxiliary_user_data *aux = wl_resource_get_user_data(resource);
	if (!aux) {
		return;
	}

	struct wlr_tablet_v2_event_feedback evt = {
		.serial = serial,
		.description = description,
		.index = aux->index
		};

	wl_signal_emit(&aux->pad->pad->events.strip_feedback, &evt);
}

static void handle_tablet_pad_strip_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_pad_strip_v2_interface tablet_pad_strip_impl = {
	.set_feedback = handle_tablet_pad_strip_v2_set_feedback,
	.destroy = handle_tablet_pad_strip_v2_destroy,
};

static void handle_tablet_pad_v2_set_feedback( struct wl_client *client,
		struct wl_resource *resource, uint32_t button,
		const char *description, uint32_t serial) {
	struct wlr_tablet_v2_tablet_pad *pad = wl_resource_get_user_data(resource);

	struct wlr_tablet_v2_event_feedback evt = {
		.serial = serial,
		.index = button,
		.description = description,
		};

	wl_signal_emit(&pad->events.button_feedback, &evt);
}

static struct zwp_tablet_pad_v2_interface tablet_pad_impl = {
	.set_feedback = handle_tablet_pad_v2_set_feedback,
	.destroy = handle_tablet_pad_v2_destroy,
};

static void destroy_tablet_pad_group_v2(struct wl_resource *resource) {
	struct tablet_pad_auxiliary_user_data *aux = wl_resource_get_user_data(resource);

	if (!aux) {
		return;
	}

	aux->pad->groups[aux->index] = NULL;
	free(aux);
	wl_resource_set_user_data(resource, NULL);
}

static void handle_tablet_pad_group_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_pad_group_v2_interface tablet_pad_group_impl = {
	.destroy = handle_tablet_pad_group_v2_destroy,
};

static void add_tablet_pad_group(struct wlr_tablet_v2_tablet_pad *pad,
		struct wlr_tablet_pad_client_v2 *client,
		struct wlr_tablet_pad_group *group, size_t index) {
	client->groups[index] =
		wl_resource_create(client->client, &zwp_tablet_pad_group_v2_interface, 1, 0);
	if (!client->groups[index]) {
		wl_client_post_no_memory(client->client);
		return;
	}
	struct tablet_pad_auxiliary_user_data *user_data =
		calloc(1, sizeof(struct tablet_pad_auxiliary_user_data));
	if (!user_data) {
		return;
	}
	user_data->pad = client;
	user_data->index = index;
	wl_resource_set_implementation(client->groups[index], &tablet_pad_group_impl,
		user_data, destroy_tablet_pad_group_v2);

	zwp_tablet_pad_v2_send_group(client->resource, client->groups[index]);
	zwp_tablet_pad_group_v2_send_modes(client->groups[index], group->mode_count);

	struct wl_array button_array;
	wl_array_init(&button_array);
	wl_array_add(&button_array, group->button_count * sizeof(int));
	memcpy(button_array.data, group->buttons, group->button_count * sizeof(int));
	zwp_tablet_pad_group_v2_send_buttons(client->groups[index], &button_array);
	wl_array_release(&button_array);

	client->strip_count = group->strip_count;
	for (size_t i = 0; i < group->strip_count; ++i) {
		size_t strip = group->strips[i];
		struct tablet_pad_auxiliary_user_data *user_data =
			calloc(1, sizeof(struct tablet_pad_auxiliary_user_data));
		if (!user_data) {
			continue;
		}
		user_data->pad = client;
		user_data->index = strip;
		client->strips[strip] =
			wl_resource_create(client->client, &zwp_tablet_pad_strip_v2_interface, 1, 0);
		wl_resource_set_implementation(client->strips[strip],
			&tablet_pad_strip_impl, user_data, destroy_tablet_pad_strip_v2);
		zwp_tablet_pad_group_v2_send_strip(client->groups[index], client->strips[strip]);
	}

	client->ring_count = group->ring_count;
	for (size_t i = 0; i < group->ring_count; ++i) {
		size_t ring = group->rings[i];
		struct tablet_pad_auxiliary_user_data *user_data =
			calloc(1, sizeof(struct tablet_pad_auxiliary_user_data));
		if (!user_data) {
			continue;
		}
		user_data->pad = client;
		user_data->index = ring;
		client->rings[ring] =
			wl_resource_create(client->client, &zwp_tablet_pad_ring_v2_interface, 1, 0);
		wl_resource_set_implementation(client->rings[ring],
			&tablet_pad_ring_impl, user_data, destroy_tablet_pad_ring_v2);
		zwp_tablet_pad_group_v2_send_ring(client->groups[index], client->rings[ring]);
	}

	zwp_tablet_pad_group_v2_send_done(client->groups[index]);
}

static void add_tablet_pad_client(struct wlr_tablet_seat_client_v2 *seat,
		struct wlr_tablet_v2_tablet_pad *pad) {
	struct wlr_tablet_pad_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_pad_client_v2));
	if (!client) {
		wl_client_post_no_memory(seat->wl_client);
		return;
	}
	client->pad = pad;

	client->groups = calloc(sizeof(int), wl_list_length(&pad->wlr_pad->groups));
	if (!client->groups) {
		wl_client_post_no_memory(seat->wl_client);
		free(client);
		return;
	}

	client->rings = calloc(sizeof(struct wl_resource*), pad->wlr_pad->ring_count);
	if (!client->rings) {
		wl_client_post_no_memory(seat->wl_client);
		free(client->groups);
		free(client);
		return;
	}

	client->strips = calloc(sizeof(struct wl_resource*), pad->wlr_pad->strip_count);
	if (!client->strips) {
		wl_client_post_no_memory(seat->wl_client);
		free(client->groups);
		free(client->rings);
		free(client);
		return;
	}

	client->resource =
		wl_resource_create(seat->wl_client, &zwp_tablet_pad_v2_interface, 1, 0);
	if (!client->resource) {
		wl_client_post_no_memory(seat->wl_client);
		free(client->groups);
		free(client->rings);
		free(client->strips);
		free(client);
		return;
	}
	wl_resource_set_implementation(client->resource, &tablet_pad_impl,
		client, destroy_tablet_pad_v2);
	zwp_tablet_seat_v2_send_pad_added(seat->resource, client->resource);
	client->client = seat->wl_client;

	// Send the expected events
	if (pad->wlr_pad->button_count) {
		zwp_tablet_pad_v2_send_buttons(client->resource, pad->wlr_pad->button_count);
	}
	for (size_t i = 0; i < pad->wlr_pad->paths.length; ++i) {
		zwp_tablet_pad_v2_send_path(client->resource,
			pad->wlr_pad->paths.items[i]);
	}
	size_t i = 0;
	struct wlr_tablet_pad_group *group;
	wl_list_for_each(group, &pad->wlr_pad->groups, link) {
		add_tablet_pad_group(pad, client, group, i++);
	}
	zwp_tablet_pad_v2_send_done(client->resource);

	wl_list_insert(&seat->pads, &client->seat_link);
	wl_list_insert(&pad->clients, &client->pad_link);
}

static void handle_wlr_tablet_pad_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_v2_tablet_pad *pad =
		wl_container_of(listener, pad, pad_destroy);

	struct wlr_tablet_pad_client_v2 *pos;
	struct wlr_tablet_pad_client_v2 *tmp;
	wl_list_for_each_safe(pos, tmp, &pad->clients, pad_link) {
		// XXX: Add a timer/flag to destroy if client is slow?
		zwp_tablet_pad_v2_send_removed(pos->resource);

		for (size_t i = 0; i < pos->group_count; ++i) {
			destroy_tablet_pad_group_v2(pos->groups[i]);
		}

		for (size_t i = 0; i < pos->strip_count; ++i) {
			destroy_tablet_pad_strip_v2(pos->strips[i]);
		}

		for (size_t i = 0; i < pos->ring_count; ++i) {
			destroy_tablet_pad_ring_v2(pos->rings[i]);
		}
	}

	wl_list_remove(&pad->clients);
	wl_list_remove(&pad->link);
	wl_list_remove(&pad->pad_destroy.link);
	wl_list_remove(&pad->events.button_feedback.listener_list);
	wl_list_remove(&pad->events.strip_feedback.listener_list);
	wl_list_remove(&pad->events.ring_feedback.listener_list);
	free(pad);
}

struct wlr_tablet_v2_tablet_pad *wlr_tablet_pad_create(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_input_device *wlr_device) {
	assert(wlr_device->type == WLR_INPUT_DEVICE_TABLET_PAD);
	struct wlr_tablet_seat_v2 *seat = get_or_make_tablet_seat(manager, wlr_seat);
	if (!seat) {
		return NULL;
	}
	struct wlr_tablet_pad *wlr_pad = wlr_device->tablet_pad;
	struct wlr_tablet_v2_tablet_pad *pad = calloc(1, sizeof(struct wlr_tablet_v2_tablet_pad));
	if (!pad) {
		return NULL;
	}

	pad->group_count = wl_list_length(&wlr_pad->groups);
	pad->groups = calloc(pad->group_count, sizeof(int));
	if (!pad->groups) {
		free(pad);
		return NULL;
	}

	pad->wlr_pad = wlr_pad;
	wl_list_init(&pad->clients);

	pad->pad_destroy.notify = handle_wlr_tablet_pad_destroy;
	wl_signal_add(&wlr_device->events.destroy, &pad->pad_destroy);
	wl_list_insert(&seat->pads, &pad->link);

	// We need to create a tablet client for all clients on the seat
	struct wlr_tablet_seat_client_v2 *pos;
	wl_list_for_each(pos, &seat->clients, seat_link) {
		// Tell the clients about the new tool
		add_tablet_pad_client(pos, pad);
	}

	wl_signal_init(&pad->events.button_feedback);
	wl_signal_init(&pad->events.strip_feedback);
	wl_signal_init(&pad->events.ring_feedback);

	return pad;
}

void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager);
static struct wlr_tablet_manager_client_v2 *tablet_manager_client_from_resource(struct wl_resource *resource);

static void tablet_seat_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_seat_v2_interface seat_impl = {
	.destroy = tablet_seat_destroy,
};

static struct wlr_tablet_seat_client_v2 *tablet_seat_from_resource (
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_seat_v2_interface,
		&seat_impl));
	return wl_resource_get_user_data(resource);
}

static void wlr_tablet_seat_client_v2_destroy(struct wl_resource *resource) {
	struct wlr_tablet_seat_client_v2 *seat = tablet_seat_from_resource(resource);

	/* XXX: Evaluate whether we should have a way to access structs */
	wl_list_remove(&seat->tools);
	wl_list_remove(&seat->tablets);
	wl_list_remove(&seat->pads);

	wl_list_remove(&seat->seat_link);
	wl_list_remove(&seat->client_link);

	free(seat);
}

static void handle_seat_client_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_seat_client_v2 *seat =
		wl_container_of(listener, seat, seat_client_destroy);
	destroy_tablet_seat_client(seat);
}

static void tablet_manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void get_tablet_seat(struct wl_client *wl_client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *seat_resource)
{
	struct wlr_tablet_manager_client_v2 *manager = tablet_manager_client_from_resource(resource);
	struct wlr_seat_client *seat = wlr_seat_client_from_resource(seat_resource);
	struct wlr_tablet_seat_v2 *tablet_seat =
		get_or_make_tablet_seat(manager->manager, seat->seat);

	if (!tablet_seat) {// This can only happen when we ran out of memory
		wl_client_post_no_memory(wl_client);
		return;
	}

	struct wlr_tablet_seat_client_v2 *seat_client =
		calloc(1, sizeof(struct wlr_tablet_seat_client_v2));
	if (tablet_seat == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	seat_client->resource =
		wl_resource_create(wl_client, &zwp_tablet_seat_v2_interface, 1, id);
	if (seat_client->resource == NULL) {
		free(seat_client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(seat_client->resource, &seat_impl, seat_client,
		wlr_tablet_seat_client_v2_destroy);


	seat_client->seat = seat;
	seat_client->client = manager;
	seat_client->wl_client = wl_client;
	wl_list_init(&seat_client->tools);
	wl_list_init(&seat_client->tablets);
	wl_list_init(&seat_client->pads);

	seat_client->seat_client_destroy.notify = handle_seat_client_destroy;
	wl_signal_add(&seat->events.destroy, &seat_client->seat_client_destroy);

	wl_list_insert(&manager->tablet_seats, &seat_client->client_link);
	wl_list_insert(&tablet_seat->clients, &seat_client->seat_link);

	// We need to emmit the devices allready on the seat
	struct wlr_tablet_v2_tablet *tablet_pos;
	wl_list_for_each(tablet_pos, &tablet_seat->tablets, link) {
		add_tablet_client(seat_client, tablet_pos);
	}

	struct wlr_tablet_v2_tablet_pad *pad_pos;
	wl_list_for_each(pad_pos, &tablet_seat->pads, link) {
		add_tablet_pad_client(seat_client, pad_pos);
	}

	struct wlr_tablet_v2_tablet_tool *tool_pos;
	wl_list_for_each(tool_pos, &tablet_seat->tools, link) {
		add_tablet_tool_client(seat_client, tool_pos);
	}
}

static struct zwp_tablet_manager_v2_interface manager_impl = {
	.get_tablet_seat = get_tablet_seat,
	.destroy = tablet_manager_destroy,
};

static struct wlr_tablet_manager_client_v2 *tablet_manager_client_from_resource (
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_manager_v2_interface,
		&manager_impl));
	return wl_resource_get_user_data(resource);
}

static void wlr_tablet_manager_v2_destroy(struct wl_resource *resource) {
	struct wlr_tablet_manager_client_v2 *client = tablet_manager_client_from_resource(resource);

	// TODO: Evaluate whether we may need to iterate structs
	wl_list_remove(&client->link);
	wl_list_remove(&client->tablet_seats);
	//wl_list_remove(&client->client_destroy.link);

	free(client);
}

static void tablet_v2_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_tablet_manager_v2 *manager = data;
	assert(wl_client && manager);

	struct wlr_tablet_manager_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_manager_client_v2));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->tablet_seats);

	client->resource =
		wl_resource_create(wl_client, &zwp_tablet_manager_v2_interface, version, id);
	if (client->resource == NULL) {
		free(client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	client->client = wl_client;
	client->manager = manager;

	wl_resource_set_implementation(client->resource, &manager_impl, client,
		wlr_tablet_manager_v2_destroy);
	wl_list_insert(&manager->clients, &client->link);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_manager_v2 *tablet =
		wl_container_of(listener, tablet, display_destroy);
	wlr_tablet_v2_destroy(tablet);
}

void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager) {
	struct wlr_tablet_manager_client_v2 *tmp;
	struct wlr_tablet_manager_client_v2 *pos;

	wl_list_for_each_safe(pos, tmp, &manager->clients, link) {
		wl_resource_destroy(pos->resource);
	}

	wl_global_destroy(manager->wl_global);
	free(manager);
}

struct wlr_tablet_manager_v2 *wlr_tablet_v2_create(struct wl_display *display) {
	struct wlr_tablet_manager_v2 *tablet =
		calloc(1, sizeof(struct wlr_tablet_manager_v2));
	if (!tablet) {
		return NULL;
	}

	wl_list_init(&tablet->clients);
	wl_list_init(&tablet->seats);

	tablet->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &tablet->display_destroy);

	tablet->wl_global = wl_global_create(display,
		&zwp_tablet_manager_v2_interface, 1, tablet, tablet_v2_bind);
	if (tablet->wl_global == NULL) {
		free(tablet);
		return NULL;
	}

	return tablet;
}

/* Actual protocol foo */
// https://www.geeksforgeeks.org/move-zeroes-end-array/
static size_t push_zeroes_to_end(uint32_t arr[], size_t n) {
	size_t count = 0;

	for (size_t i = 0; i < n; i++) {
		if (arr[i] != 0) {
			arr[count++] = arr[i];
		}
	}

	size_t ret = count;

	while (count < n) {
		arr[count++] = 0;
	}

	return ret;
}

static void tablet_tool_button_update(struct wlr_tablet_v2_tablet_tool *tool,
		uint32_t button, enum zwp_tablet_pad_v2_button_state state) {
	bool found = false;
	size_t i = 0;
	for (; i < tool->num_buttons; ++i) {
		if (tool->pressed_buttons[i] == button) {
			found = true;
			break;
		}
	}

	if (button == ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED && !found &&
			tool->num_buttons < WLR_TABLEt_V2_TOOL_BUTTONS_CAP) {
		tool->pressed_buttons[tool->num_buttons++] = button;
	}
	if (button == ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED && found) {
		tool->pressed_buttons[i] = 0;
		tool->num_buttons = push_zeroes_to_end(tool->pressed_buttons, WLR_TABLEt_V2_TOOL_BUTTONS_CAP);
	}

	assert(tool->num_buttons <= WLR_TABLEt_V2_TOOL_BUTTONS_CAP);
}

static void send_tool_frame(void *data) {
	struct wlr_tablet_tool_client_v2 *tool = data;

	zwp_tablet_tool_v2_send_frame(tool->resource, 0);
	tool->frame_source = NULL;
}

static void queue_tool_frame(struct wlr_tablet_tool_client_v2 *tool) {
	struct wl_display *display = wl_client_get_display(tool->client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	if (!tool->frame_source) {
		tool->frame_source =
			wl_event_loop_add_idle(loop, send_tool_frame, tool);
	}
}

uint32_t wlr_send_tablet_v2_tablet_tool_proximity_in(
		struct wlr_tablet_v2_tablet_tool *tool,
		struct wlr_tablet_v2_tablet *tablet,
		struct wlr_surface *surface) {
	struct wl_client *client = wl_resource_get_client(surface->resource);

	if (tool->focused_surface == surface) {
		return 0;
	}

	struct wlr_tablet_client_v2 *tablet_tmp;
	struct wlr_tablet_client_v2 *tablet_client = NULL;
	wl_list_for_each(tablet_tmp, &tablet->clients, tablet_link) {
		if (tablet_tmp->client == client) {
			tablet_client = tablet_tmp;
			break;
		}
	}

	// Couldn't find the client binding for the surface's client. Either
	// the client didn't bind tablet_v2 at all, or not for the relevant
	// seat
	if (!tablet_client) {
		return 0;
	}

	struct wlr_tablet_tool_client_v2 *tool_tmp;
	struct wlr_tablet_tool_client_v2 *tool_client;
	wl_list_for_each(tool_tmp, &tool->clients, tool_link) {
		if (tool_tmp->client == client) {
			tool_client = tool_tmp;
			break;
		}
	}

	// Couldn't find the client binding for the surface's client. Either
	// the client didn't bind tablet_v2 at all, or not for the relevant
	// seat
	if (!tool_client) {
		return 0;
	}

	tool->current_client = tool_client;

	/* Pre-increment keeps 0 clean. wraparound would be after 2^32
	 * proximity_in. Someone wants to do the math how long that would take?
	 */
	uint32_t serial = ++tool_client->proximity_serial;

	zwp_tablet_tool_v2_send_proximity_in(tool_client->resource, serial,
		tablet_client->resource, surface->resource);
	queue_tool_frame(tool_client);

	tool->focused_surface = surface;
	return serial;
}

void wlr_send_tablet_v2_tablet_tool_motion(
		struct wlr_tablet_v2_tablet_tool *tool, double x, double y) {
	if (!tool->current_client) {
		return;
	}

	zwp_tablet_tool_v2_send_motion(tool->current_client->resource,
		wl_fixed_from_double(x), wl_fixed_from_double(y));

	queue_tool_frame(tool->current_client);
}

void wlr_send_tablet_v2_tablet_tool_proximity_out(
		struct wlr_tablet_v2_tablet_tool *tool) {
	if (tool->current_client) {
		zwp_tablet_tool_v2_send_proximity_out(tool->current_client->resource);
		// XXX: Get the time for the frame
		if (tool->current_client->frame_source) {
			wl_event_source_remove(tool->current_client->frame_source);
			send_tool_frame(tool->current_client);
		}
		tool->current_client = NULL;
	}
}

void wlr_send_tablet_v2_tablet_tool_distance(
		struct wlr_tablet_v2_tablet_tool *tool, uint32_t distance) {
	if (tool->current_client) {
		zwp_tablet_tool_v2_send_distance(tool->current_client->resource,
			distance);

		queue_tool_frame(tool->current_client);
	}
}

uint32_t wlr_send_tablet_v2_tablet_tool_button(
		struct wlr_tablet_v2_tablet_tool *tool, uint32_t button,
		enum zwp_tablet_pad_v2_button_state state) {
	tablet_tool_button_update(tool, button, state);

	if (tool->current_client) {
		uint32_t serial = ++tool->button_serial;

		zwp_tablet_tool_v2_send_button(tool->current_client->resource,
			serial, button, state);
		queue_tool_frame(tool->current_client);

		return serial;
	}

	return 0;
}

void wlr_send_tablet_v2_tablet_tool_wheel(
	struct wlr_tablet_v2_tablet_tool *tool, double delta, int32_t clicks) {
	if (tool->current_client) {
		zwp_tablet_tool_v2_send_wheel(tool->current_client->resource,
			clicks, delta);

		queue_tool_frame(tool->current_client);
	}
}

uint32_t wlr_send_tablet_v2_tablet_pad_enter(
		struct wlr_tablet_v2_tablet_pad *pad,
		struct wlr_tablet_v2_tablet *tablet,
		struct wlr_surface *surface) {
	struct wl_client *client = wl_resource_get_client(surface->resource);

	struct wlr_tablet_client_v2 *tablet_tmp;
	struct wlr_tablet_client_v2 *tablet_client = NULL;
	wl_list_for_each(tablet_tmp, &tablet->clients, tablet_link) {
		if (tablet_tmp->client == client) {
			tablet_client = tablet_tmp;
			break;
		}
	}

	// Couldn't find the client binding for the surface's client. Either
	// the client didn't bind tablet_v2 at all, or not for the relevant
	// seat
	if (!tablet_client) {
		return 0;
	}

	struct wlr_tablet_pad_client_v2 *pad_tmp;
	struct wlr_tablet_pad_client_v2 *pad_client;
	wl_list_for_each(pad_tmp, &pad->clients, pad_link) {
		if (pad_tmp->client == client) {
			pad_client = pad_tmp;
			break;
		}
	}

	// Couldn't find the client binding for the surface's client. Either
	// the client didn't bind tablet_v2 at all, or not for the relevant
	// seat
	if (!pad_client) {
		return 0;
	}

	pad->current_client = pad_client;

	/* Pre-increment keeps 0 clean. wraparound would be after 2^32
	 * proximity_in. Someone wants to do the math how long that would take?
	 */
	uint32_t serial = ++pad_client->enter_serial;

	zwp_tablet_pad_v2_send_enter(pad_client->resource, serial,
		tablet_client->resource, surface->resource);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint32_t time = now.tv_nsec / 1000;

	for (size_t i = 0; i < pad->group_count; ++i) {
		zwp_tablet_pad_group_v2_send_mode_switch(
			pad_client->groups[i], time, serial, pad->groups[i]);
	}

	return serial;
}

void wlr_send_tablet_v2_tablet_pad_button(
		struct wlr_tablet_v2_tablet_pad *pad, size_t button,
		uint32_t time, enum zwp_tablet_pad_v2_button_state state) {

	if (pad->current_client) {
		zwp_tablet_pad_v2_send_button(pad->current_client->resource,
				time, button, state);
	}
}

void wlr_send_tablet_v2_tablet_pad_strip(struct wlr_tablet_v2_tablet_pad *pad,
		uint32_t strip, double position, bool finger, uint32_t time) {
	if (!pad->current_client &&
			pad->current_client->strips &&
			pad->current_client->strips[strip]) {
		return;
	}
	struct wl_resource *resource = pad->current_client->strips[strip];

	if (finger) {
		zwp_tablet_pad_strip_v2_send_source(resource, ZWP_TABLET_PAD_STRIP_V2_SOURCE_FINGER);
	}

	if (position < 0) {
		zwp_tablet_pad_strip_v2_send_stop(resource);
	} else {
		zwp_tablet_pad_strip_v2_send_position(resource, position * 65535);
	}
	zwp_tablet_pad_strip_v2_send_frame(resource, time);
}

void wlr_send_tablet_v2_tablet_pad_ring(struct wlr_tablet_v2_tablet_pad *pad,
		uint32_t ring, double position, bool finger, uint32_t time) {
	if (!pad->current_client ||
			!pad->current_client->rings ||
			!pad->current_client->rings[ring]) {
		return;
	}
	struct wl_resource *resource = pad->current_client->rings[ring];

	if (finger) {
		zwp_tablet_pad_ring_v2_send_source(resource, ZWP_TABLET_PAD_RING_V2_SOURCE_FINGER);
	}

	if (position < 0) {
		zwp_tablet_pad_ring_v2_send_stop(resource);
	} else {
		zwp_tablet_pad_ring_v2_send_angle(resource, position);
	}
	zwp_tablet_pad_ring_v2_send_frame(resource, time);
}

uint32_t wlr_send_tablet_v2_tablet_pad_leave(struct wlr_tablet_v2_tablet_pad *pad,
		struct wlr_surface *surface) {
	if (!pad->current_client ||
			wl_resource_get_client(surface->resource) != pad->current_client->client) {
		return 0;
	}

	/* Pre-increment keeps 0 clean. wraparound would be after 2^32
	 * proximity_in. Someone wants to do the math how long that would take?
	 */
	uint32_t serial = ++pad->current_client->leave_serial;

	zwp_tablet_pad_v2_send_leave(pad->current_client->resource, serial, surface->resource);
	return serial;
}

uint32_t wlr_send_tablet_v2_tablet_pad_mode(struct wlr_tablet_v2_tablet_pad *pad,
		size_t group, uint32_t mode, uint32_t time) {
	if (!pad->current_client ||
			!pad->current_client->groups ||
			!pad->current_client->groups[group] ) {
		return 0;
	}

	if (pad->groups[group] == mode) {
		return 0;
	}

	pad->groups[group] = mode;

	/* Pre-increment keeps 0 clean. wraparound would be after 2^32
	 * proximity_in. Someone wants to do the math how long that would take?
	 */
	uint32_t serial = ++pad->current_client->mode_serial;

	zwp_tablet_pad_group_v2_send_mode_switch(
		pad->current_client->groups[group], time, serial, mode);
	return serial;
}

bool wlr_surface_accepts_tablet_v2(struct wlr_tablet_v2_tablet *tablet,
		struct wlr_surface *surface) {
	struct wl_client *client = wl_resource_get_client(surface->resource);

	if (tablet->current_client &&
			tablet->current_client->client == client) {
		return true;
	}

	struct wlr_tablet_client_v2 *tablet_tmp;
	wl_list_for_each(tablet_tmp, &tablet->clients, tablet_link) {
		if (tablet_tmp->client == client) {
			return true;
		}
	}

	return false;
}
