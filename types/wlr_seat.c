#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include <wlr/backend.h>

static void seat_get_pointer(struct wl_client *client, struct wl_resource *res, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: seat get pointer");
}

static void seat_get_keyboard(struct wl_client *client, struct wl_resource *res, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: seat get keyboard");
}

static void seat_get_touch(struct wl_client *client, struct wl_resource *res, uint32_t id) {
	wlr_log(L_DEBUG, "TODO: seat get touch");
}

static const struct wl_seat_interface wl_seat_implementation = {
	.get_pointer = seat_get_pointer,
	.get_keyboard = seat_get_keyboard,
	.get_touch = seat_get_touch
};

static void seat_update_caps(struct wlr_seat *seat) {
	wlr_log(L_DEBUG, "TODO: send updated caps to client resources");
}

static void seat_input_add(struct wl_listener *listener, void *data) {
	struct wlr_seat *seat = wl_container_of(data, seat, listener.input_add);
	struct wlr_input_device *dev = data;
	assert(seat && dev);

	switch (dev->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		if (!seat->keyboard) {
			seat->keyboard = wlr_wl_keyboard_create(seat, dev);
			seat->caps |= WL_SEAT_CAPABILITY_KEYBOARD;
			seat_update_caps(seat);
		}
		break;
	case WLR_INPUT_DEVICE_POINTER:
		if (!seat->pointer) {
			seat->pointer = wlr_wl_pointer_create(seat, dev);
			seat->caps |= WL_SEAT_CAPABILITY_POINTER;
			seat_update_caps(seat);
		}
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		if (!seat->touch) {
			seat->touch = wlr_wl_touch_create(seat, dev);
			seat->caps |= WL_SEAT_CAPABILITY_TOUCH;
			seat_update_caps(seat);
		}
		break;
	default:
		return;
	}
}

static void seat_input_remove(struct wl_listener *listener, void *data) {
	struct wlr_seat *seat = wl_container_of(data, seat, listener.input_add);
	struct wlr_input_device *dev = data;
	assert(seat && dev);

	switch (dev->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		if (seat->keyboard && seat->keyboard->device == dev) {
			wlr_wl_keyboard_destroy(seat->keyboard);
			seat->keyboard = NULL;
			seat->caps &= ~WL_SEAT_CAPABILITY_TOUCH;
			seat_update_caps(seat);
		}
		break;
	case WLR_INPUT_DEVICE_POINTER:
		if (seat->pointer && seat->pointer->device == dev) {
			wlr_wl_keyboard_destroy(seat->pointer);
			seat->pointer = NULL;
			seat->caps &= ~WL_SEAT_CAPABILITY_POINTER;
			seat_update_caps(seat);
		}
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		if (seat->touch && seat->touch->device == dev) {
			wlr_wl_keyboard_destroy(seat->touch);
			seat->touch = NULL;
			seat->caps &= ~WL_SEAT_CAPABILITY_TOUCH;
			seat_update_caps(seat);
		}
		break;
	default:
		return;
	}
}

static void wl_seat_destroy(struct wl_resource *resource) {
}

static void wl_seat_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wlr_seat *seat = data;
	assert(client && seat);
	if (version > 4) {
		wlr_log(L_ERROR, "Client requested unsupported wl_seat version, disconnecting");
		wl_client_destroy(client);
		return;
	}

	struct wl_resource *resource = wl_resource_create(
			client, &wl_seat_interface, version, id);
	wl_resource_set_implementation(resource, &wl_seat_implementation, seat, wl_seat_destroy);
	wl_seat_send_capabilities(resource, seat->caps);

	if (version >= 2) {
		const char *xdg_seat = getenv("XDG_SEAT");
		wl_seat_send_name(resource, (xdg_seat ? xdg_seat : "seat0"));
	}
}


struct wlr_seat *wlr_seat_create(struct wl_display *display, struct wlr_backend *backend) {
	struct wlr_seat *seat;
	if (!(seat = calloc(1, sizeof(struct wlr_seat)))) {
		wlr_log(L_ERROR, "Failed to allocate seat");
		return NULL;
	}

	seat->listener.input_add.notify = seat_input_add;
	seat->listener.input_remove.notify = seat_input_remove;

	wl_signal_add(&backend->events.input_add, &seat->listener.input_add);
	wl_signal_add(&backend->events.input_add, &seat->listener.input_remove);

	seat->global = wl_global_create(display, &wl_seat_interface, 4, seat,
		&wl_seat_bind);
	return seat;
}

void wlr_seat_destroy(struct wlr_seat *seat) {
	wl_global_destroy(seat->global);

	if (seat->keyboard) {
		wlr_wl_keyboard_destroy(seat->keyboard);
	}

	if (seat->pointer) {
		wlr_wl_keyboard_destroy(seat->pointer);
	}

	if (seat->touch) {
		wlr_wl_keyboard_destroy(seat->touch);
	}

	wl_list_remove(&seat->listener.input_add.link);
	wl_list_remove(&seat->listener.input_remove.link);
	free(seat);
}
