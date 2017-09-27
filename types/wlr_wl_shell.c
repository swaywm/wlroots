#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_wl_shell.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>

static void shell_surface_pong(struct wl_client *client,
		struct wl_resource *resource, uint32_t serial) {
	wlr_log(L_DEBUG, "got shell surface pong");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	if (surface->ping_serial != serial) {
		return;
	}

	wl_event_source_timer_update(surface->ping_timer, 0);
	surface->ping_serial = 0;
}

static void shell_surface_move(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial) {
	wlr_log(L_DEBUG, "got shell surface move");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);

	struct wlr_wl_shell_surface_move_event *event =
		calloc(1, sizeof(struct wlr_wl_shell_surface_move_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->seat_handle = seat_handle;
	event->serial = serial;

	wl_signal_emit(&surface->events.request_move, event);

	free(event);
}

static void shell_surface_resize(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, uint32_t edges) {
	wlr_log(L_DEBUG, "got shell surface resize");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);

	struct wlr_wl_shell_surface_resize_event *event =
		calloc(1, sizeof(struct wlr_wl_shell_surface_resize_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->seat_handle = seat_handle;
	event->serial = serial;
	event->edges = edges;

	wl_signal_emit(&surface->events.request_resize, event);

	free(event);
}

static void shell_surface_set_toplevel(struct wl_client *client,
		struct wl_resource *resource) {
	wlr_log(L_DEBUG, "got shell surface toplevel");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);

	if (surface->role != WLR_WL_SHELL_SURFACE_ROLE_NONE) {
		return;
	}

	surface->role = WLR_WL_SHELL_SURFACE_ROLE_TOPLEVEL;

	wl_signal_emit(&surface->events.set_role, surface);
}

static void shell_surface_set_transient(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *parent_resource,
		int32_t x, int32_t y, uint32_t flags) {
	wlr_log(L_DEBUG, "got shell surface transient");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_wl_shell_surface *parent =
		wl_resource_get_user_data(parent_resource);
	// TODO: check if parent_resource == NULL?

	if (surface->role != WLR_WL_SHELL_SURFACE_ROLE_NONE) {
		return;
	}

	struct wlr_wl_shell_surface_transient_state *state =
		calloc(1, sizeof(struct wlr_wl_shell_surface_transient_state));
	if (state == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	state->parent = parent;
	state->x = x;
	state->y = y;
	state->flags = flags;

	free(surface->transient_state);
	surface->transient_state = state;

	surface->role = WLR_WL_SHELL_SURFACE_ROLE_TRANSCIENT;

	wl_signal_emit(&surface->events.set_role, surface);
}

static void shell_surface_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, uint32_t method, uint32_t framerate,
		struct wl_resource *output_resource) {
	wlr_log(L_DEBUG, "got shell surface fullscreen");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wl_resource_get_user_data(output_resource);
	}

	if (surface->role == WLR_WL_SHELL_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	struct wlr_wl_shell_surface_set_fullscreen_event *event =
		calloc(1, sizeof(struct wlr_wl_shell_surface_set_fullscreen_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->method = method;
	event->framerate = framerate;
	event->output = output;

	wl_signal_emit(&surface->events.request_set_fullscreen, event);

	free(event);
}

static void shell_surface_set_popup(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *seat_resource,
		uint32_t serial, struct wl_resource *parent_resource, int32_t x, int32_t y,
		uint32_t flags) {
	wlr_log(L_DEBUG, "got shell surface popup");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_seat_handle *seat_handle =
		wl_resource_get_user_data(seat_resource);
	struct wlr_wl_shell_surface *parent =
		wl_resource_get_user_data(parent_resource);
	// TODO: check if parent_resource == NULL?

	if (surface->role != WLR_WL_SHELL_SURFACE_ROLE_NONE) {
		return;
	}

	struct wlr_wl_shell_surface_transient_state *transcient_state =
		calloc(1, sizeof(struct wlr_wl_shell_surface_transient_state));
	if (transcient_state == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	transcient_state->parent = parent;
	transcient_state->x = x;
	transcient_state->y = y;
	transcient_state->flags = flags;

	struct wlr_wl_shell_surface_popup_state *popup_state =
		calloc(1, sizeof(struct wlr_wl_shell_surface_transient_state));
	if (popup_state == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	popup_state->seat_handle = seat_handle;
	popup_state->serial = serial;

	free(surface->transient_state);
	surface->transient_state = transcient_state;

	free(surface->popup_state);
	surface->popup_state = popup_state;

	surface->role = WLR_WL_SHELL_SURFACE_ROLE_POPUP;

	wl_signal_emit(&surface->events.set_role, surface);
}

static void shell_surface_set_maximized(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource) {
	wlr_log(L_DEBUG, "got shell surface maximized");
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	struct wlr_output *output = NULL;
	if (output_resource != NULL) {
		output = wl_resource_get_user_data(output_resource);
	}

	if (surface->role == WLR_WL_SHELL_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	struct wlr_wl_shell_surface_set_maximized_event *event =
		calloc(1, sizeof(struct wlr_wl_shell_surface_set_maximized_event));
	if (event == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	event->client = client;
	event->surface = surface;
	event->output = output;

	wl_signal_emit(&surface->events.request_set_maximized, event);

	free(event);
}

static void shell_surface_set_title(struct wl_client *client,
		struct wl_resource *resource, const char *title) {
	wlr_log(L_DEBUG, "new shell surface title: %s", title);
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);

	char *tmp = strdup(title);
	if (tmp == NULL) {
		return;
	}

	free(surface->title);
	surface->title = tmp;

	wl_signal_emit(&surface->events.set_title, surface);
}

static void shell_surface_set_class(struct wl_client *client,
		struct wl_resource *resource, const char *class) {
	wlr_log(L_DEBUG, "new shell surface class: %s", class);
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);

	char *tmp = strdup(class);
	if (tmp == NULL) {
		return;
	}

	free(surface->class);
	surface->class = tmp;

	wl_signal_emit(&surface->events.set_class, surface);
}

struct wl_shell_surface_interface shell_surface_interface = {
	.pong = shell_surface_pong,
	.move = shell_surface_move,
	.resize = shell_surface_resize,
	.set_toplevel = shell_surface_set_toplevel,
	.set_transient = shell_surface_set_transient,
	.set_fullscreen = shell_surface_set_fullscreen,
	.set_popup = shell_surface_set_popup,
	.set_maximized = shell_surface_set_maximized,
	.set_title = shell_surface_set_title,
	.set_class = shell_surface_set_class,
};

static void destroy_shell_surface(struct wl_resource *resource) {
	struct wlr_wl_shell_surface *surface = wl_resource_get_user_data(resource);
	wl_signal_emit(&surface->events.destroy, surface);
	wl_list_remove(&surface->link);
	free(surface->transient_state);
	free(surface->popup_state);
	free(surface->title);
	free(surface->class);
	free(surface);
}

static int wlr_wl_shell_surface_ping_timeout(void *user_data) {
	struct wlr_wl_shell_surface *surface = user_data;
	wl_signal_emit(&surface->events.ping_timeout, surface);

	surface->ping_serial = 0;
	return 1;
}

static void wl_shell_get_shell_surface(struct wl_client *client,
		struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_surface *surface = wl_resource_get_user_data(surface_resource);
	struct wlr_wl_shell *wl_shell = wl_resource_get_user_data(resource);
	struct wlr_wl_shell_surface *wl_surface =
		calloc(1, sizeof(struct wlr_wl_shell_surface));
	if (wl_surface == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_surface->shell = wl_shell;
	wl_surface->client = client;
	wl_surface->resource = surface_resource;
	wl_surface->surface = surface;

	struct wl_resource *shell_surface_resource = wl_resource_create(client,
			&wl_shell_surface_interface, wl_resource_get_version(resource), id);
	wlr_log(L_DEBUG, "New wl_shell %p (res %p)", wl_surface, shell_surface_resource);
	wl_resource_set_implementation(shell_surface_resource,
			&shell_surface_interface, wl_surface, destroy_shell_surface);

	wl_signal_init(&wl_surface->events.destroy);
	wl_signal_init(&wl_surface->events.ping_timeout);
	wl_signal_init(&wl_surface->events.request_move);
	wl_signal_init(&wl_surface->events.request_resize);
	wl_signal_init(&wl_surface->events.request_set_fullscreen);
	wl_signal_init(&wl_surface->events.request_set_maximized);
	wl_signal_init(&wl_surface->events.set_role);
	wl_signal_init(&wl_surface->events.set_title);
	wl_signal_init(&wl_surface->events.set_class);

	struct wl_display *display = wl_client_get_display(client);
	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	wl_surface->ping_timer = wl_event_loop_add_timer(loop,
		wlr_wl_shell_surface_ping_timeout, wl_surface);
	if (wl_surface->ping_timer == NULL) {
		wl_client_post_no_memory(client);
	}

	wl_list_insert(&wl_shell->surfaces, &wl_surface->link);
	wl_signal_emit(&wl_shell->events.new_surface, wl_surface);
}

static struct wl_shell_interface wl_shell_impl = {
	.get_shell_surface = wl_shell_get_shell_surface
};

static void wl_shell_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void wl_shell_bind(struct wl_client *wl_client, void *_wl_shell,
		uint32_t version, uint32_t id) {
	struct wlr_wl_shell *wl_shell = _wl_shell;
	assert(wl_client && wl_shell);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported wl_shell version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &wl_shell_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_shell_impl,
			wl_shell, wl_shell_destroy);
	wl_list_insert(&wl_shell->wl_resources, wl_resource_get_link(wl_resource));
}

struct wlr_wl_shell *wlr_wl_shell_create(struct wl_display *display) {
	struct wlr_wl_shell *wl_shell = calloc(1, sizeof(struct wlr_wl_shell));
	if (!wl_shell) {
		return NULL;
	}
	wl_shell->ping_timeout = 10000;
	struct wl_global *wl_global = wl_global_create(display,
		&wl_shell_interface, 1, wl_shell, wl_shell_bind);
	if (!wl_global) {
		free(wl_shell);
		return NULL;
	}
	wl_shell->wl_global = wl_global;
	wl_list_init(&wl_shell->wl_resources);
	wl_list_init(&wl_shell->surfaces);
	wl_signal_init(&wl_shell->events.new_surface);
	return wl_shell;
}

void wlr_wl_shell_destroy(struct wlr_wl_shell *wlr_wl_shell) {
	if (!wlr_wl_shell) {
		return;
	}
	struct wl_resource *resource = NULL, *temp = NULL;
	wl_resource_for_each_safe(resource, temp, &wlr_wl_shell->wl_resources) {
		struct wl_list *link = wl_resource_get_link(resource);
		wl_list_remove(link);
	}
	// TODO: destroy surfaces
	// TODO: this segfault (wl_display->registry_resource_list is not init)
	// wl_global_destroy(wlr_wl_shell->wl_global);
	free(wlr_wl_shell);
}

void wlr_wl_shell_surface_ping(struct wlr_wl_shell_surface *surface) {
	if (surface->ping_serial != 0) {
		// already pinged
		return;
	}

	surface->ping_serial =
		wl_display_next_serial(wl_client_get_display(surface->client));
	wl_event_source_timer_update(surface->ping_timer,
		surface->shell->ping_timeout);
	wl_shell_surface_send_ping(surface->resource, surface->ping_serial);
}

void wlr_wl_shell_surface_configure(struct wlr_wl_shell_surface *surface,
		uint32_t edges, int32_t width, int32_t height) {
	wl_shell_surface_send_configure(surface->resource, edges, width, height);
}

void wlr_wl_shell_surface_popup_done(struct wlr_wl_shell_surface *surface) {
	wl_shell_surface_send_popup_done(surface->resource);
}
