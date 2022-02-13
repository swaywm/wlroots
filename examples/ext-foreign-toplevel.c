#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "foreign-toplevel-info-unstable-v1-client-protocol.h"
#include "foreign-toplevel-management-unstable-v1-client-protocol.h"

#define FOREIGN_TOPLEVEL_INFO_VERSION 1
#define FOREIGN_TOPLEVEL_MANAGEMENT_VERSION 1

/**
 * Usage:
 * 1. ext-foreign-toplevel
 * 	  Prints a list of opened toplevels
 * 2. ext-foreign-toplevel -f <id>
 *    Focus the toplevel with the given id
 * 3. ext-foreign-toplevel -a <id>
 *    Maximize the toplevel with the given id
 * 4. ext-foreign-toplevel -u <id>
 *    Unmaximize the toplevel with the given id
 * 5. ext-foreign-toplevel -i <id>
 *    Minimize the toplevel with the given id
 * 6. ext-foreign-toplevel -r <id>
 * 	  Restore(unminimize) the toplevel with the given id
 * 7. ext-foreign-toplevel -c <id>
 *    Close the toplevel with the given id
 * 8. ext-foreign-toplevel -m
 *    Continuously print changes to the list of opened toplevels.
 *    Can be used together with some of the previous options.
 */

enum toplevel_state_field {
	TOPLEVEL_STATE_MAXIMIZED = (1 << 0),
	TOPLEVEL_STATE_MINIMIZED = (1 << 1),
	TOPLEVEL_STATE_ACTIVATED = (1 << 2),
	TOPLEVEL_STATE_FULLSCREEN = (1 << 3),
	TOPLEVEL_STATE_INVALID = (1 << 4),
};

static const uint32_t no_parent = (uint32_t)-1;

struct toplevel_state {
	char *title;
	char *app_id;

	uint32_t state;
	uint32_t parent_id;
};

static void copy_state(struct toplevel_state *current,
		struct toplevel_state *pending) {
	if (current->title && pending->title) {
		free(current->title);
	}
	if (current->app_id && pending->app_id) {
		free(current->app_id);
	}

	if (pending->title) {
		current->title = pending->title;
		pending->title = NULL;
	}
	if (pending->app_id) {
		current->app_id = pending->app_id;
		pending->app_id = NULL;
	}

	if (!(pending->state & TOPLEVEL_STATE_INVALID)) {
		current->state = pending->state;
	}

	current->parent_id = pending->parent_id;

	pending->state = TOPLEVEL_STATE_INVALID;
}

static uint32_t global_id = 0;
struct toplevel_v1 {
	struct wl_list link;
	struct zext_foreign_toplevel_handle_v1 *handle;

	uint32_t id;
	struct toplevel_state current, pending;
};

static void print_toplevel(struct toplevel_v1 *toplevel, bool print_endl) {
	printf("-> %d. title=%s app_id=%s", toplevel->id,
			toplevel->current.title ?: "(nil)",
			toplevel->current.app_id ?: "(nil)");

	if (toplevel->current.parent_id != no_parent) {
		printf(" parent=%u", toplevel->current.parent_id);
	} else {
		printf(" no parent");
	}

	if (print_endl) {
		printf("\n");
	}
}

static void print_toplevel_state(struct toplevel_v1 *toplevel, bool print_endl) {
	if (toplevel->current.state & TOPLEVEL_STATE_MAXIMIZED) {
		printf(" maximized");
	} else {
		printf(" unmaximized");
	}
	if (toplevel->current.state & TOPLEVEL_STATE_MINIMIZED) {
		printf(" minimized");
	} else {
		printf(" unminimized");
	}
	if (toplevel->current.state & TOPLEVEL_STATE_ACTIVATED) {
		printf(" active");
	} else {
		printf(" inactive");
	}
	if (toplevel->current.state & TOPLEVEL_STATE_FULLSCREEN) {
		printf(" fullscreen");
	}

	if (print_endl) {
		printf("\n");
	}
}

static void toplevel_handle_title(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle,
		const char *title) {
	struct toplevel_v1 *toplevel = data;
	free(toplevel->pending.title);
	toplevel->pending.title = strdup(title);
}

static void toplevel_handle_app_id(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle,
		const char *app_id) {
	struct toplevel_v1 *toplevel = data;
	free(toplevel->pending.app_id);
	toplevel->pending.app_id = strdup(app_id);
}

static void toplevel_handle_output_enter(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle,
		struct wl_output *output) {
	struct toplevel_v1 *toplevel = data;
	print_toplevel(toplevel, false);
	printf(" enter output %u\n",
		(uint32_t)(size_t)wl_output_get_user_data(output));
}

static void toplevel_handle_output_leave(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle,
		struct wl_output *output) {
	struct toplevel_v1 *toplevel = data;
	print_toplevel(toplevel, false);
	printf(" leave output %u\n",
		(uint32_t)(size_t)wl_output_get_user_data(output));
}

static uint32_t array_to_state(struct wl_array *array) {
	uint32_t state = 0;
	uint32_t *entry;
	wl_array_for_each(entry, array) {
		if (*entry == ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED)
			state |= TOPLEVEL_STATE_MAXIMIZED;
		if (*entry == ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
			state |= TOPLEVEL_STATE_MINIMIZED;
		if (*entry == ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
			state |= TOPLEVEL_STATE_ACTIVATED;
		if (*entry == ZEXT_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN)
			state |= TOPLEVEL_STATE_FULLSCREEN;
	}

	return state;
}

static void toplevel_handle_state(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle,
		struct wl_array *state) {
	struct toplevel_v1 *toplevel = data;
	toplevel->pending.state = array_to_state(state);
}

static struct zext_foreign_toplevel_info_v1 *toplevel_info = NULL;
static struct zext_foreign_toplevel_manager_v1 *toplevel_manager = NULL;
static struct wl_list toplevel_list;

static void toplevel_handle_parent(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle,
		struct zext_foreign_toplevel_handle_v1 *parent) {
	struct toplevel_v1 *toplevel = data;
	toplevel->pending.parent_id = no_parent;
	if (parent) {
		struct toplevel_v1 *toplevel_tmp;
		wl_list_for_each(toplevel_tmp, &toplevel_list, link) {
			if (toplevel_tmp->handle == parent) {
				toplevel->pending.parent_id = toplevel_tmp->id;
				break;
			}
		}
		if (toplevel->pending.parent_id == no_parent) {
			fprintf(stderr, "Cannot find parent toplevel!\n");
		}
	}
}

static void toplevel_handle_done(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle) {
	struct toplevel_v1 *toplevel = data;
	bool state_changed = toplevel->current.state != toplevel->pending.state;

	copy_state(&toplevel->current, &toplevel->pending);

	print_toplevel(toplevel, !state_changed);
	if (state_changed) {
		print_toplevel_state(toplevel, true);
	}
}

static void toplevel_handle_closed(void *data,
		struct zext_foreign_toplevel_handle_v1 *handle) {
	struct toplevel_v1 *toplevel = data;
	print_toplevel(toplevel, false);
	printf(" closed\n");

	zext_foreign_toplevel_handle_v1_destroy(handle);
}

static const struct zext_foreign_toplevel_handle_v1_listener toplevel_impl = {
	.title = toplevel_handle_title,
	.app_id = toplevel_handle_app_id,
	.output_enter = toplevel_handle_output_enter,
	.output_leave = toplevel_handle_output_leave,
	.state = toplevel_handle_state,
	.done = toplevel_handle_done,
	.closed = toplevel_handle_closed,
	.parent = toplevel_handle_parent
};

static void toplevel_manager_handle_toplevel(void *data,
		struct zext_foreign_toplevel_info_v1 *toplevel_info,
		struct zext_foreign_toplevel_handle_v1 *handle) {
	struct toplevel_v1 *toplevel = calloc(1, sizeof(struct toplevel_v1));
	if (!toplevel) {
		fprintf(stderr, "Failed to allocate memory for toplevel\n");
		return;
	}

	toplevel->id = global_id++;
	toplevel->handle = handle;
	toplevel->current.parent_id = no_parent;
	toplevel->pending.parent_id = no_parent;
	wl_list_insert(&toplevel_list, &toplevel->link);

	zext_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_impl,
		toplevel);
}

static void toplevel_manager_handle_finished(void *data,
		struct zext_foreign_toplevel_info_v1 *toplevel_info) {
	zext_foreign_toplevel_info_v1_destroy(toplevel_info);
}

static const struct zext_foreign_toplevel_info_v1_listener toplevel_info_impl = {
	.toplevel = toplevel_manager_handle_toplevel,
	.finished = toplevel_manager_handle_finished,
};

struct wl_seat *seat = NULL;
static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *output = wl_registry_bind(registry, name,
				&wl_output_interface, version);
		wl_output_set_user_data(output, (void*)(size_t)name); // assign some ID to the output
	} else if (strcmp(interface,
			zext_foreign_toplevel_info_v1_interface.name) == 0) {
		toplevel_info = wl_registry_bind(registry, name,
				&zext_foreign_toplevel_info_v1_interface,
				FOREIGN_TOPLEVEL_INFO_VERSION);

		wl_list_init(&toplevel_list);
		zext_foreign_toplevel_info_v1_add_listener(toplevel_info,
				&toplevel_info_impl, NULL);
	} else if (strcmp(interface,
			zext_foreign_toplevel_manager_v1_interface.name) == 0) {
		toplevel_manager = wl_registry_bind(registry, name,
				&zext_foreign_toplevel_manager_v1_interface,
				FOREIGN_TOPLEVEL_MANAGEMENT_VERSION);
	} else if (strcmp(interface, wl_seat_interface.name) == 0 && seat == NULL) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

/* return NULL when id == -1
 * exit if the given ID cannot be found in the list of toplevels */
static struct toplevel_v1 *toplevel_by_id_or_bail(int32_t id) {
	if (id == -1) {
		return NULL;
	}

	struct toplevel_v1 *toplevel;
	wl_list_for_each(toplevel, &toplevel_list, link) {
		if (toplevel->id == (uint32_t)id) {
			return toplevel;
		}
	}

	fprintf(stderr, "No toplevel with the given id: %d\n", id);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int focus_id = -1, close_id = -1;
	int maximize_id = -1, unmaximize_id = -1;
	int minimize_id = -1, restore_id = -1;
	int fullscreen_id = -1, unfullscreen_id = -1;
	int one_shot = 1;
	int c;

	// TODO maybe print usage with -h?
	while ((c = getopt(argc, argv, "f:a:u:i:r:c:s:S:m")) != -1) {
		switch (c) {
		case 'f':
			focus_id = atoi(optarg);
			break;
		case 'a':
			maximize_id = atoi(optarg);
			break;
		case 'u':
			unmaximize_id = atoi(optarg);
			break;
		case 'i':
			minimize_id = atoi(optarg);
			break;
		case 'r':
			restore_id = atoi(optarg);
			break;
		case 'c':
			close_id = atoi(optarg);
			break;
		case 's':
			fullscreen_id = atoi(optarg);
			break;
		case 'S':
			unfullscreen_id = atoi(optarg);
			break;
		case 'm':
			one_shot = 0;
			break;
		}
	}

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Failed to create display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (toplevel_info == NULL) {
		fprintf(stderr, "ext-foreign-toplevel-info not available\n");
		return EXIT_FAILURE;
	}
	if (toplevel_manager == NULL) {
		fprintf(stderr, "ext-foreign-toplevel-management not available\n");
		return EXIT_FAILURE;
	}

	wl_display_roundtrip(display); // load list of toplevels
	wl_display_roundtrip(display); // load toplevel details

	struct toplevel_v1 *toplevel;
	if ((toplevel = toplevel_by_id_or_bail(focus_id))) {
		zext_foreign_toplevel_manager_v1_activate(toplevel_manager, toplevel->handle, seat);
	}
	if ((toplevel = toplevel_by_id_or_bail(maximize_id))) {
		zext_foreign_toplevel_manager_v1_set_maximized(toplevel_manager, toplevel->handle);
	}
	if ((toplevel = toplevel_by_id_or_bail(unmaximize_id))) {
		zext_foreign_toplevel_manager_v1_unset_maximized(toplevel_manager, toplevel->handle);
	}
	if ((toplevel = toplevel_by_id_or_bail(minimize_id))) {
		zext_foreign_toplevel_manager_v1_set_minimized(toplevel_manager, toplevel->handle);
	}
	if ((toplevel = toplevel_by_id_or_bail(restore_id))) {
		zext_foreign_toplevel_manager_v1_unset_minimized(toplevel_manager, toplevel->handle);
	}
	if ((toplevel = toplevel_by_id_or_bail(fullscreen_id))) {
		zext_foreign_toplevel_manager_v1_set_fullscreen(toplevel_manager, toplevel->handle, NULL);
	}
	if ((toplevel = toplevel_by_id_or_bail(unfullscreen_id))) {
		zext_foreign_toplevel_manager_v1_unset_fullscreen(toplevel_manager, toplevel->handle);
	}
	if ((toplevel = toplevel_by_id_or_bail(close_id))) {
		zext_foreign_toplevel_manager_v1_close(toplevel_manager, toplevel->handle);
	}

	wl_display_flush(display);

	if (one_shot == 0) {
		while (wl_display_dispatch(display) != -1) {
			// This space intentionally left blank
		}
	}

	return EXIT_SUCCESS;
}
