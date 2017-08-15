#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include <wlr/backend.h>

static void destroy_resource(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_implementation = {
	.release = destroy_resource,
};

static const struct wl_keyboard_interface keyboard_implementation = {
	.release = destroy_resource,
};

static const struct wl_touch_interface touch_implementation = {
	.release = destroy_resource,
};

static void destroy_pointer(struct wl_resource *resource) {
	struct wlr_seat *seat = wl_resource_get_user_data(resource);
	struct wlr_seat_resources *res = wlr_seat_resources_for_client(seat,
		wl_resource_get_client(resource));
	if (res) {
		res->pointer = NULL;
	}
}

static void destroy_keyboard(struct wl_resource *resource) {
	struct wlr_seat *seat = wl_resource_get_user_data(resource);
	struct wlr_seat_resources *res = wlr_seat_resources_for_client(seat,
		wl_resource_get_client(resource));
	if (res) {
		res->keyboard = NULL;
	}
}

static void destroy_touch(struct wl_resource *resource) {
	struct wlr_seat *seat = wl_resource_get_user_data(resource);
	struct wlr_seat_resources *res = wlr_seat_resources_for_client(seat,
		wl_resource_get_client(resource));
	if (res) {
		res->touch = NULL;
	}
}

static void seat_get_pointer(struct wl_client *client, struct wl_resource *res, uint32_t id) {
	struct wlr_seat *seat = wl_resource_get_user_data(res);
	struct wlr_seat_resources *resources = wlr_seat_resources_for_client(seat, client);
	struct wl_resource *pointer = wl_resource_create(client, &wl_pointer_interface,
		wl_resource_get_version(res), id);
	if (!pointer) {
		wlr_log(L_ERROR, "Failed to create pointer resource");
		wl_resource_post_no_memory(res);
		return;
	}

	wl_resource_set_implementation(pointer, &pointer_implementation, seat, &destroy_pointer);
	resources->pointer = pointer;
}

static void seat_get_keyboard(struct wl_client *client, struct wl_resource *res, uint32_t id) {
	struct wlr_seat *seat = wl_resource_get_user_data(res);
	int version = wl_resource_get_version(res);
	struct wlr_seat_resources *resources = wlr_seat_resources_for_client(seat, client);
	struct wl_resource *keyboard = wl_resource_create(client, &wl_keyboard_interface,
		version, id);
	if (!keyboard) {
		wlr_log(L_ERROR, "Failed to create keyboard resource");
		wl_resource_post_no_memory(res);
		return;
	}

	wl_resource_set_implementation(keyboard, &keyboard_implementation, seat, &destroy_keyboard);
	resources->keyboard = keyboard;

	if (seat->keyboard.keymap_fd) {
		wl_keyboard_send_keymap(keyboard, seat->keyboard.keymap_format,
			seat->keyboard.keymap_fd, seat->keyboard.keymap_size);
	}

	if (version >= 4) {
		wl_keyboard_send_repeat_info(keyboard, seat->keyboard.repeat_rate,
			seat->keyboard.repeat_delay);
	}
}

static void seat_get_touch(struct wl_client *client, struct wl_resource *res, uint32_t id) {
	struct wlr_seat *seat = wl_resource_get_user_data(res);
	struct wlr_seat_resources *resources = wlr_seat_resources_for_client(seat, client);
	struct wl_resource *touch = wl_resource_create(client, &wl_touch_interface,
		wl_resource_get_version(res), id);
	if (!touch) {
		wlr_log(L_ERROR, "Failed to create touch resource");
		wl_resource_post_no_memory(res);
		return;
	}

	wl_resource_set_implementation(touch, &touch_implementation, seat, &destroy_touch);
	resources->touch = touch;
}

static const struct wl_seat_interface seat_implementation = {
	.get_pointer = seat_get_pointer,
	.get_keyboard = seat_get_keyboard,
	.get_touch = seat_get_touch,
	.release = destroy_resource,
};

static void seat_destroy(struct wl_resource *resource) {
	struct wlr_seat *seat = wl_resource_get_user_data(resource);
	for(unsigned int i = 0; i < seat->resources.length; ++i) {
		struct wlr_seat_resources *resources = seat->resources.items[i];
		if (resources->seat == resource) {
			if (resources == seat->pointer_over_res) {
				seat->pointer_over_res = NULL;
			}

			if (resources == seat->keyboard_focus_res) {
				seat->keyboard_focus_res = NULL;
			}

			list_del(&seat->resources, i);
			free(resources);
			break;
		}
	}
}

static void seat_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wlr_seat *seat = data;
	assert(client && seat);
	if (version > 6) {
		wlr_log(L_ERROR, "Client requested unsupported wl_seat version, disconnecting");
		wl_client_destroy(client);
		return;
	}

	struct wl_resource *resource = wl_resource_create(
			client, &wl_seat_interface, version, id);
	if (!resource) {
		wlr_log(L_ERROR, "Failed to create seat resource");
		wl_client_post_no_memory(client);
		return;
	}

	struct wlr_seat_resources *resources;
	if (!(resources = calloc(1, sizeof(*resources)))) {
		wlr_log(L_ERROR, "Failed to alloc seat resources");
		wl_resource_destroy(resource);
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &seat_implementation, seat, seat_destroy);
	wl_seat_send_capabilities(resource, seat->caps);
	resources->seat = resource;
	list_add(&seat->resources, resources);

	if (version >= 2) {
		wl_seat_send_name(resource, seat->name);
	}
}

struct wlr_seat *wlr_seat_create(struct wl_display *display, const char *name) {
	struct wlr_seat *seat;
	if (!(seat = calloc(1, sizeof(struct wlr_seat)))) {
		wlr_log(L_ERROR, "Failed to allocate seat");
		return NULL;
	}

	if (name == NULL) {
		const char *env_name = getenv("XDG_SEAT");
		name = env_name ? env_name : "seat0";
	}

	seat->global = wl_global_create(display, &wl_seat_interface, 6, seat, &seat_bind);
	seat->name = name;
	return seat;
}

void wlr_seat_destroy(struct wlr_seat *seat) {
	wl_global_destroy(seat->global);
	free(seat);
}

void wlr_seat_set_caps(struct wlr_seat *seat, uint32_t caps) {
	seat->caps = caps;
	for(unsigned int i = 0; i < seat->resources.length; ++i) {
		struct wlr_seat_resources *resources = seat->resources.items[i];
		wl_seat_send_capabilities(resources->seat, caps);
	}
}

void wlr_seat_pointer_move(struct wlr_seat *seat, uint32_t time, wl_fixed_t surface_x,
		wl_fixed_t surface_y) {
	if (!seat->pointer_over_res || !seat->pointer_over_res->pointer) {
		return;
	}

	wl_pointer_send_motion(seat->pointer_over_res->pointer, time, surface_x, surface_y);
}

void wlr_seat_pointer_enter(struct wlr_seat *seat, uint32_t serial,
		struct wl_resource *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	// TODO: send pointer_leave event if pointer_over_* is not NULL?
	struct wlr_seat_resources *res = wlr_seat_resources_for_client(seat,
		wl_resource_get_client(surface));
	seat->pointer_over_surf = surface;
	if (!res) {
		return;
	}

	seat->pointer_over_res = res;
	wl_pointer_send_enter(seat->pointer_over_res->pointer, serial, surface, surface_x, surface_y);
}

void wlr_seat_pointer_leave(struct wlr_seat *seat, uint32_t serial, struct wl_resource *surface) {
	if (!seat->pointer_over_res || !seat->pointer_over_res->pointer) {
		return;
	}

	if (surface != seat->pointer_over_surf) {
		wlr_log(L_DEBUG, "inconsistent pointer_over surface");
		return;
	}

	wl_pointer_send_leave(seat->pointer_over_res->pointer, serial, surface);
	seat->pointer_over_res = NULL;
	seat->pointer_over_surf = NULL;
}

void wlr_set_pointer_button(struct wlr_seat *seat, uint32_t serial, uint32_t time,
		uint32_t button, uint8_t state) {
	if (!seat->pointer_over_res || !seat->pointer_over_res->pointer) {
		return;
	}

	wl_pointer_send_button(seat->pointer_over_res->pointer, serial, time, button, state);
}

void wlr_seat_keyboard_key(struct wlr_seat *seat, uint32_t serial, uint32_t time,
		uint32_t key, uint8_t state) {
	if (!seat->keyboard_focus_res || !seat->keyboard_focus_res->keyboard) {
		return;
	}

	wl_keyboard_send_key(seat->keyboard_focus_res->keyboard, serial, time, key, state);
}

void wlr_seat_keyboard_focus(struct wlr_seat *seat, uint32_t serial, struct wl_resource *surface,
		struct wl_array *keys) {
	// TODO: send pointer_leave event if keyboard_focus_* is not NULL?
	struct wlr_seat_resources *res = wlr_seat_resources_for_client(seat,
		wl_resource_get_client(surface));
	seat->keyboard_focus_surf = surface;
	if (!res) {
		return;
	}

	seat->keyboard_focus_res = res;
	wl_keyboard_send_enter(seat->keyboard_focus_res->keyboard, serial, surface, keys);
}

void wlr_seat_keyboard_modifiers(struct wlr_seat *seat, uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	if (!seat->keyboard_focus_res || !seat->keyboard_focus_res->keyboard) {
		return;
	}

	wl_keyboard_send_modifiers(seat->keyboard_focus_res->keyboard, serial, mods_depressed,
		mods_latched, mods_locked, group);
}

void wlr_seat_keyboard_keymap(struct wlr_seat *seat, uint32_t format, int fd, uint32_t size) {
	seat->keyboard.keymap_format = format;
	seat->keyboard.keymap_fd = fd;
	seat->keyboard.keymap_size = size;

	for(unsigned int i = 0; i < seat->resources.length; ++i) {
		struct wlr_seat_resources *resources = seat->resources.items[i];
		if (resources->keyboard) {
			wl_keyboard_send_keymap(resources->keyboard, format, fd, size);
		}
	}
}

void wlr_seat_keyboard_repeat_info(struct wlr_seat *seat, int32_t rate, int32_t delay) {
	 seat->keyboard.repeat_rate = rate;
	 seat->keyboard.repeat_delay = delay;

	for(unsigned int i = 0; i < seat->resources.length; ++i) {
		struct wlr_seat_resources *resources = seat->resources.items[i];
		if (resources->keyboard) {
			wl_keyboard_send_repeat_info(resources->keyboard, rate, delay);
		}
	}
}

struct wlr_seat_resources *wlr_seat_resources_for_client(struct wlr_seat *seat,
		struct wl_client *client) {
	for(unsigned int i = 0; i < seat->resources.length; ++i) {
		struct wlr_seat_resources *resources = seat->resources.items[i];
		if (client == wl_resource_get_client(resources->seat)) {
			return resources;
		}
	}

	return NULL;
}
