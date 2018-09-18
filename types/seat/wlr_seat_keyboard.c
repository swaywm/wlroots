#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/util/log.h>
#include "types/wlr_seat.h"
#include "util/signal.h"
#include "util/os-compatibility.h"

static void default_keyboard_enter(struct wlr_seat_keyboard_grab *grab,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	wlr_seat_keyboard_enter(grab->seat, surface, keycodes, num_keycodes, modifiers);
}

static void default_keyboard_key(struct wlr_seat_keyboard_grab *grab,
		uint32_t time, uint32_t key, uint32_t state) {
	wlr_seat_keyboard_send_key(grab->seat, time, key, state);
}

static void default_keyboard_modifiers(struct wlr_seat_keyboard_grab *grab,
		struct wlr_keyboard_modifiers *modifiers) {
	wlr_seat_keyboard_send_modifiers(grab->seat, modifiers);
}

static void default_keyboard_cancel(struct wlr_seat_keyboard_grab *grab) {
	// cannot be cancelled
}

const struct wlr_keyboard_grab_interface default_keyboard_grab_impl = {
	.enter = default_keyboard_enter,
	.key = default_keyboard_key,
	.modifiers = default_keyboard_modifiers,
	.cancel = default_keyboard_cancel,
};


static void keyboard_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_impl = {
	.release = keyboard_release,
};

static struct wlr_seat_client *seat_client_from_keyboard_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_keyboard_interface,
		&keyboard_impl));
	return wl_resource_get_user_data(resource);
}

static void keyboard_handle_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
	seat_client_destroy_keyboard(resource);
}


void wlr_seat_keyboard_send_key(struct wlr_seat *wlr_seat, uint32_t time,
		uint32_t key, uint32_t state) {
	struct wlr_seat_client *client = wlr_seat->keyboard_state.focused_client;
	if (!client) {
		return;
	}

	uint32_t serial = wl_display_next_serial(wlr_seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		if (seat_client_from_keyboard_resource(resource) == NULL) {
			continue;
		}

		wl_keyboard_send_key(resource, serial, time, key, state);
	}
}

static void seat_client_send_keymap(struct wlr_seat_client *client,
	struct wlr_keyboard *keyboard);

static void handle_keyboard_keymap(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard_state *state =
		wl_container_of(listener, state, keyboard_keymap);
	struct wlr_seat_client *client;
	struct wlr_keyboard *keyboard = data;
	if (keyboard == state->keyboard) {
		wl_list_for_each(client, &state->seat->clients, link) {
			seat_client_send_keymap(client, state->keyboard);
		}
	}
}

static void seat_client_send_repeat_info(struct wlr_seat_client *client,
	struct wlr_keyboard *keyboard);

static void handle_keyboard_repeat_info(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state =
		wl_container_of(listener, state, keyboard_repeat_info);
	struct wlr_seat_client *client;
	wl_list_for_each(client, &state->seat->clients, link) {
		seat_client_send_repeat_info(client, state->keyboard);
	}
}

static void handle_keyboard_destroy(struct wl_listener *listener, void *data) {
	struct wlr_seat_keyboard_state *state =
		wl_container_of(listener, state, keyboard_destroy);
	wlr_seat_set_keyboard(state->seat, NULL);
}

void wlr_seat_set_keyboard(struct wlr_seat *seat,
		struct wlr_input_device *device) {
	// TODO call this on device key event before the event reaches the
	// compositor and set a pending keyboard and then send the new keyboard
	// state on the next keyboard notify event.
	struct wlr_keyboard *keyboard = (device ? device->keyboard : NULL);
	if (seat->keyboard_state.keyboard == keyboard) {
		return;
	}

	if (seat->keyboard_state.keyboard) {
		wl_list_remove(&seat->keyboard_state.keyboard_destroy.link);
		wl_list_remove(&seat->keyboard_state.keyboard_keymap.link);
		wl_list_remove(&seat->keyboard_state.keyboard_repeat_info.link);
		seat->keyboard_state.keyboard = NULL;
	}

	if (keyboard) {
		assert(device->type == WLR_INPUT_DEVICE_KEYBOARD);
		seat->keyboard_state.keyboard = keyboard;

		wl_signal_add(&device->events.destroy,
			&seat->keyboard_state.keyboard_destroy);
		seat->keyboard_state.keyboard_destroy.notify = handle_keyboard_destroy;
		wl_signal_add(&device->keyboard->events.keymap,
			&seat->keyboard_state.keyboard_keymap);
		seat->keyboard_state.keyboard_keymap.notify = handle_keyboard_keymap;
		wl_signal_add(&device->keyboard->events.repeat_info,
			&seat->keyboard_state.keyboard_repeat_info);
		seat->keyboard_state.keyboard_repeat_info.notify =
			handle_keyboard_repeat_info;

		struct wlr_seat_client *client;
		wl_list_for_each(client, &seat->clients, link) {
			seat_client_send_keymap(client, keyboard);
			seat_client_send_repeat_info(client, keyboard);
		}

		wlr_seat_keyboard_send_modifiers(seat, &keyboard->modifiers);
	} else {
		seat->keyboard_state.keyboard = NULL;
	}
}

struct wlr_keyboard *wlr_seat_get_keyboard(struct wlr_seat *seat) {
	return seat->keyboard_state.keyboard;
}

void wlr_seat_keyboard_start_grab(struct wlr_seat *wlr_seat,
		struct wlr_seat_keyboard_grab *grab) {
	grab->seat = wlr_seat;
	wlr_seat->keyboard_state.grab = grab;

	wlr_signal_emit_safe(&wlr_seat->events.keyboard_grab_begin, grab);
}

void wlr_seat_keyboard_end_grab(struct wlr_seat *wlr_seat) {
	struct wlr_seat_keyboard_grab *grab = wlr_seat->keyboard_state.grab;

	if (grab != wlr_seat->keyboard_state.default_grab) {
		wlr_seat->keyboard_state.grab = wlr_seat->keyboard_state.default_grab;
		wlr_signal_emit_safe(&wlr_seat->events.keyboard_grab_end, grab);
		if (grab->interface->cancel) {
			grab->interface->cancel(grab);
		}
	}
}

static void seat_keyboard_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_seat_keyboard_state *state = wl_container_of(
			listener, state, surface_destroy);
	wl_list_remove(&state->surface_destroy.link);
	wl_list_init(&state->surface_destroy.link);
	wlr_seat_keyboard_clear_focus(state->seat);
}

void wlr_seat_keyboard_send_modifiers(struct wlr_seat *seat,
		struct wlr_keyboard_modifiers *modifiers) {
	struct wlr_seat_client *client = seat->keyboard_state.focused_client;
	if (client == NULL) {
		return;
	}

	uint32_t serial = wl_display_next_serial(seat->display);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		if (seat_client_from_keyboard_resource(resource) == NULL) {
			continue;
		}

		if (modifiers == NULL) {
			wl_keyboard_send_modifiers(resource, serial, 0, 0, 0, 0);
		} else {
			wl_keyboard_send_modifiers(resource, serial,
				modifiers->depressed, modifiers->latched,
				modifiers->locked, modifiers->group);
		}
	}
}

void wlr_seat_keyboard_enter(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	if (seat->keyboard_state.focused_surface == surface) {
		// this surface already got an enter notify
		return;
	}

	struct wlr_seat_client *client = NULL;

	if (surface) {
		struct wl_client *wl_client = wl_resource_get_client(surface->resource);
		client = wlr_seat_client_for_wl_client(seat, wl_client);
	}

	struct wlr_seat_client *focused_client =
		seat->keyboard_state.focused_client;
	struct wlr_surface *focused_surface =
		seat->keyboard_state.focused_surface;

	// leave the previously entered surface
	if (focused_client != NULL && focused_surface != NULL) {
		uint32_t serial = wl_display_next_serial(seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &focused_client->keyboards) {
			if (seat_client_from_keyboard_resource(resource) == NULL) {
				continue;
			}
			wl_keyboard_send_leave(resource, serial, focused_surface->resource);
		}
	}

	// enter the current surface
	if (client != NULL) {
		struct wl_array keys;
		wl_array_init(&keys);
		for (size_t i = 0; i < num_keycodes; ++i) {
			uint32_t *p = wl_array_add(&keys, sizeof(uint32_t));
			if (!p) {
				wlr_log(WLR_ERROR, "Cannot allocate memory, skipping keycode: %d\n",
					keycodes[i]);
				continue;
			}
			*p = keycodes[i];
		}
		uint32_t serial = wl_display_next_serial(seat->display);
		struct wl_resource *resource;
		wl_resource_for_each(resource, &client->keyboards) {
			if (seat_client_from_keyboard_resource(resource) == NULL) {
				continue;
			}
			wl_keyboard_send_enter(resource, serial, surface->resource, &keys);
		}
		wl_array_release(&keys);

		wlr_seat_client_send_selection(client);
		wlr_seat_client_send_primary_selection(client);
	}

	// reinitialize the focus destroy events
	wl_list_remove(&seat->keyboard_state.surface_destroy.link);
	wl_list_init(&seat->keyboard_state.surface_destroy.link);
	if (surface) {
		wl_signal_add(&surface->events.destroy,
			&seat->keyboard_state.surface_destroy);
		seat->keyboard_state.surface_destroy.notify =
			seat_keyboard_handle_surface_destroy;
	}

	seat->keyboard_state.focused_client = client;
	seat->keyboard_state.focused_surface = surface;

	if (client != NULL) {
		// tell new client about any modifier change last,
		// as it targets seat->keyboard_state.focused_client
		wlr_seat_keyboard_send_modifiers(seat, modifiers);
	}
}

void wlr_seat_keyboard_notify_enter(struct wlr_seat *seat,
		struct wlr_surface *surface, uint32_t keycodes[], size_t num_keycodes,
		struct wlr_keyboard_modifiers *modifiers) {
	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->enter(grab, surface, keycodes, num_keycodes, modifiers);
}

void wlr_seat_keyboard_clear_focus(struct wlr_seat *seat) {
	// TODO respect grabs here?
	wlr_seat_keyboard_enter(seat, NULL, NULL, 0, NULL);
}

bool wlr_seat_keyboard_has_grab(struct wlr_seat *seat) {
	return seat->keyboard_state.grab->interface != &default_keyboard_grab_impl;
}

void wlr_seat_keyboard_notify_modifiers(struct wlr_seat *seat,
		struct wlr_keyboard_modifiers *modifiers) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->modifiers(grab, modifiers);
}

void wlr_seat_keyboard_notify_key(struct wlr_seat *seat, uint32_t time,
		uint32_t key, uint32_t state) {
	clock_gettime(CLOCK_MONOTONIC, &seat->last_event);
	struct wlr_seat_keyboard_grab *grab = seat->keyboard_state.grab;
	grab->interface->key(grab, time, key, state);
}


static void seat_client_send_keymap(struct wlr_seat_client *client,
		struct wlr_keyboard *keyboard) {
	if (!keyboard) {
		return;
	}

	// TODO: We should probably lift all of the keys set by the other
	// keyboard
	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		if (seat_client_from_keyboard_resource(resource) == NULL) {
			continue;
		}

		int keymap_fd = os_create_anonymous_file(keyboard->keymap_size);
		if (keymap_fd < 0) {
			wlr_log(WLR_ERROR, "creating a keymap file for %zu bytes failed", keyboard->keymap_size);
			continue;
		}

		void *ptr = mmap(NULL, keyboard->keymap_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, keymap_fd, 0);
		if (ptr == MAP_FAILED) {
			wlr_log(WLR_ERROR, "failed to mmap() %zu bytes", keyboard->keymap_size);
			close(keymap_fd);
			continue;
		}

		strcpy(ptr, keyboard->keymap_string);
		munmap(ptr, keyboard->keymap_size);

		wl_keyboard_send_keymap(resource,
			WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymap_fd,
			keyboard->keymap_size);

		close(keymap_fd);
	}
}

static void seat_client_send_repeat_info(struct wlr_seat_client *client,
		struct wlr_keyboard *keyboard) {
	if (!keyboard) {
		return;
	}

	struct wl_resource *resource;
	wl_resource_for_each(resource, &client->keyboards) {
		if (seat_client_from_keyboard_resource(resource) == NULL) {
			continue;
		}

		if (wl_resource_get_version(resource) >=
				WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
			wl_keyboard_send_repeat_info(resource,
				keyboard->repeat_info.rate, keyboard->repeat_info.delay);
		}
	}
}

void seat_client_create_keyboard(struct wlr_seat_client *seat_client,
		uint32_t version, uint32_t id) {
	struct wl_resource *resource = wl_resource_create(seat_client->client,
		&wl_keyboard_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(seat_client->client);
		return;
	}
	wl_resource_set_implementation(resource, &keyboard_impl, seat_client,
		keyboard_handle_resource_destroy);
	wl_list_insert(&seat_client->keyboards, wl_resource_get_link(resource));

	struct wlr_keyboard *keyboard = seat_client->seat->keyboard_state.keyboard;
	seat_client_send_keymap(seat_client, keyboard);
	seat_client_send_repeat_info(seat_client, keyboard);

	// TODO possibly handle the case where this keyboard needs an enter
	// right away
}

void seat_client_destroy_keyboard(struct wl_resource *resource) {
	struct wlr_seat_client *seat_client =
		seat_client_from_keyboard_resource(resource);
	if (seat_client == NULL) {
		return;
	}
	wl_resource_set_user_data(resource, NULL);
}
