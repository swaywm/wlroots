#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "input-method-unstable-v2-protocol.h"
#include "util/signal.h"

static const struct zwp_input_method_v2_interface input_method_impl;

static struct wlr_input_method_v2 *input_method_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_method_v2_interface, &input_method_impl));
	return wl_resource_get_user_data(resource);
}

static void input_method_destroy(struct wlr_input_method_v2 *input_method) {
	wlr_signal_emit_safe(&input_method->events.destroy, input_method);
	wl_list_remove(&input_method->seat_destroy.link);
	free(input_method->pending.commit_text);
	free(input_method->pending.preedit.text);
	free(input_method->current.commit_text);
	free(input_method->current.preedit.text);
	free(input_method);
}

static void input_method_resource_destroy(struct wl_resource *resource) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method_destroy(input_method);
}

static void im_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void im_commit(struct wl_client *client, struct wl_resource *resource,
		uint32_t serial) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method->current = input_method->pending;
	input_method->current_serial = serial;
	struct wlr_input_method_v2_state default_state = {0};
	input_method->pending = default_state;
	wlr_signal_emit_safe(&input_method->events.commit, (void*)input_method);
}

static void im_commit_string(struct wl_client *client,
		struct wl_resource *resource, const char *text) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	free(input_method->pending.commit_text);
	input_method->pending.commit_text = strdup(text);
}

static void im_set_preedit_string(struct wl_client *client,
		struct wl_resource *resource, const char *text, int32_t cursor_begin,
		int32_t cursor_end) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method->pending.preedit.cursor_begin = cursor_begin;
	input_method->pending.preedit.cursor_end = cursor_end;
	free(input_method->pending.preedit.text);
	input_method->pending.preedit.text = strdup(text);
}

static void im_delete_surrounding_text(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t before_length, uint32_t after_length) {
	struct wlr_input_method_v2 *input_method =
		input_method_from_resource(resource);
	if (!input_method) {
		return;
	}
	input_method->pending.delete.before_length = before_length;
	input_method->pending.delete.after_length = after_length;
}

static void im_get_input_popup_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface) {
	wlr_log(WLR_INFO, "Stub: zwp_input_method_v2::get_input_popup_surface");
}


static void im_grab_keyboard(struct wl_client *client,
		struct wl_resource *resource, uint32_t keyboard) {
	wlr_log(WLR_INFO, "Stub: zwp_input_method_v2::grab_keyboard");
}

static const struct zwp_input_method_v2_interface input_method_impl = {
	.destroy = im_destroy,
	.commit = im_commit,
	.commit_string = im_commit_string,
	.set_preedit_string = im_set_preedit_string,
	.delete_surrounding_text = im_delete_surrounding_text,
	.get_input_popup_surface = im_get_input_popup_surface,
	.grab_keyboard = im_grab_keyboard,
};

void wlr_input_method_v2_send_activate(
		struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_activate(input_method->resource);
	input_method->active = true;
}

void wlr_input_method_v2_send_deactivate(
		struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_deactivate(input_method->resource);
	input_method->active = false;
}

void wlr_input_method_v2_send_surrounding_text(
		struct wlr_input_method_v2 *input_method, const char *text,
		uint32_t cursor, uint32_t anchor) {
	const char *send_text = text;
	if (!send_text) {
		send_text = "";
	}
	zwp_input_method_v2_send_surrounding_text(input_method->resource, send_text,
		cursor, anchor);
}

void wlr_input_method_v2_send_text_change_cause(
		struct wlr_input_method_v2 *input_method, uint32_t cause) {
	zwp_input_method_v2_send_text_change_cause(input_method->resource, cause);
}

void wlr_input_method_v2_send_content_type(
		struct wlr_input_method_v2 *input_method,
		uint32_t hint, uint32_t purpose) {
	zwp_input_method_v2_send_content_type(input_method->resource, hint,
		purpose);
}

void wlr_input_method_v2_send_done(struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_done(input_method->resource);
	input_method->client_active = input_method->active;
	input_method->current_serial++;
}

void wlr_input_method_v2_send_unavailable(
		struct wlr_input_method_v2 *input_method) {
	zwp_input_method_v2_send_unavailable(input_method->resource);
	struct wl_resource *resource = input_method->resource;
	input_method_destroy(input_method);
	wl_resource_set_user_data(resource, NULL);
}

static const struct zwp_input_method_manager_v2_interface
	input_method_manager_impl;

static struct wlr_input_method_manager_v2 *input_method_manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource,
		&zwp_input_method_manager_v2_interface, &input_method_manager_impl));
	return wl_resource_get_user_data(resource);
}

static void input_method_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_input_method_v2 *input_method = wl_container_of(listener,
		input_method, seat_destroy);
	wlr_input_method_v2_send_unavailable(input_method);
}

static void manager_get_input_method(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat,
		uint32_t input_method_id) {
	struct wlr_input_method_manager_v2 *im_manager =
		input_method_manager_from_resource(resource);

	struct wlr_input_method_v2 *input_method = calloc(1,
		sizeof(struct wlr_input_method_v2));
	if (!input_method) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_signal_init(&input_method->events.commit);
	wl_signal_init(&input_method->events.destroy);
	int version = wl_resource_get_version(resource);
	struct wl_resource *im_resource = wl_resource_create(client,
		&zwp_input_method_v2_interface, version, input_method_id);
	if (im_resource == NULL) {
		free(input_method);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(im_resource, &input_method_impl,
		input_method, input_method_resource_destroy);

	struct wlr_seat_client *seat_client = wlr_seat_client_from_resource(seat);
	wl_signal_add(&seat_client->events.destroy,
		&input_method->seat_destroy);
	input_method->seat_destroy.notify =
		input_method_handle_seat_destroy;

	input_method->resource = im_resource;
	input_method->seat = seat_client->seat;
	wl_list_insert(&im_manager->input_methods,
		wl_resource_get_link(input_method->resource));
	wlr_signal_emit_safe(&im_manager->events.input_method, input_method);
}

static void manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zwp_input_method_manager_v2_interface
		input_method_manager_impl = {
	.get_input_method = manager_get_input_method,
	.destroy = manager_destroy,
};

static void input_method_manager_unbind(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void input_method_manager_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	assert(wl_client);
	struct wlr_input_method_manager_v2 *im_manager = data;

	struct wl_resource *bound_resource = wl_resource_create(wl_client,
		&zwp_input_method_manager_v2_interface, version, id);
	if (bound_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(bound_resource, &input_method_manager_impl,
		im_manager, input_method_manager_unbind);
	wl_list_insert(&im_manager->bound_resources,
		wl_resource_get_link(bound_resource));
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_method_manager_v2 *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_input_method_manager_v2_destroy(manager);
}

struct wlr_input_method_manager_v2 *wlr_input_method_manager_v2_create(
		struct wl_display *display) {
	struct wlr_input_method_manager_v2 *im_manager = calloc(1,
		sizeof(struct wlr_input_method_manager_v2));
	if (!im_manager) {
		return NULL;
	}
	wl_signal_init(&im_manager->events.input_method);
	wl_list_init(&im_manager->bound_resources);
	wl_list_init(&im_manager->input_methods);

	im_manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &im_manager->display_destroy);

	im_manager->global = wl_global_create(display,
		&zwp_input_method_manager_v2_interface, 1, im_manager,
		input_method_manager_bind);
	return im_manager;
}

void wlr_input_method_manager_v2_destroy(
		struct wlr_input_method_manager_v2 *manager) {
	wlr_signal_emit_safe(&manager->events.destroy, manager);
	wl_list_remove(&manager->display_destroy.link);

	struct wl_resource *resource, *resource_tmp;
	wl_resource_for_each_safe(resource, resource_tmp,
			&manager->bound_resources) {
		wl_resource_destroy(resource);
	}
	struct wlr_input_method_v2 *im, *im_tmp;
	wl_list_for_each_safe(im, im_tmp, &manager->input_methods, link) {
		wl_resource_destroy(im->resource);
	}
	wl_global_destroy(manager->global);
	free(manager);
}
