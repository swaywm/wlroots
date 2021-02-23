#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "ext-workspace-unstable-v1-client-protocol.h"

#define WLR_EXT_WORKSPACE_VERSION 1

/**
 * Usage:
 * 1. ext-workspace
 *    List all workspace groups and their workspaces
 * 2. ext-workspace -w X
 *    Focus workspace with name X
 * 3. ext-workspace -m
 *    Continuously monitor for changes and print new state.
 * 4. ext-workspace -c X
 *    Create a new workspace with name hint X in some workspace group
 * 5. ext-workspace -r X
 *    Request removal of workspace X
 */

enum workspace_state_field {
	WORKSPACE_FOCUSED = (1 << 0),
};

struct workspace_state {
	char *name;
	struct wl_array coordinates;
	uint32_t state;
};

static void copy_state(struct workspace_state *current,
		struct workspace_state *pending) {

	current->state = pending->state;
	wl_array_copy(&current->coordinates, &pending->coordinates);

	if (pending->name) {
		free(current->name);
		current->name = pending->name;
		pending->name = NULL;
	}
}

struct workspace_v1 {
	struct wl_list link;
	struct zext_workspace_handle_v1 *handle;
	struct workspace_state current, pending;
};

static void print_workspace(struct workspace_v1 *workspace) {
	printf("--> workspace name=%s, focused=%d, coordinates=(",
			workspace->current.name ?: "(n/a)",
			!!(workspace->current.state & WORKSPACE_FOCUSED));

	bool is_first = true;
	int32_t *pos;
	wl_array_for_each(pos, &workspace->current.coordinates) {
		if (!is_first) {
			printf(",");
		}
		printf("%d", *pos);
		is_first = false;
	}

	printf(")\n");
}

static uint32_t array_to_state(struct wl_array *array) {
	uint32_t state = 0;
	uint32_t *entry;
	wl_array_for_each(entry, array) {
		if (*entry == ZEXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE)
			state |= WORKSPACE_FOCUSED;
	}

	return state;
}

static void workspace_handle_name(void *data,
		struct zext_workspace_handle_v1 *workspace_handle_v1,
		const char *name) {
	struct workspace_v1 *workspace = (struct workspace_v1*)
		zext_workspace_handle_v1_get_user_data(workspace_handle_v1);

	free(workspace->pending.name);
	workspace->pending.name = strdup(name);
}

static void workspace_handle_coordinates(void *data,
		struct zext_workspace_handle_v1 *workspace_handle,
		struct wl_array *coordinates) {
	struct workspace_v1 *workspace = (struct workspace_v1*)
		zext_workspace_handle_v1_get_user_data(workspace_handle);
	wl_array_copy(&workspace->pending.coordinates, coordinates);
}

static void workspace_handle_state(void *data,
		struct zext_workspace_handle_v1 *workspace_handle,
		struct wl_array *state) {
	struct workspace_v1 *workspace = (struct workspace_v1*)
		zext_workspace_handle_v1_get_user_data(workspace_handle);
	workspace->pending.state = array_to_state(state);
}

static void workspace_handle_remove(void *data,
		struct zext_workspace_handle_v1 *workspace_handle) {
	struct workspace_v1 *workspace = (struct workspace_v1*)
		zext_workspace_handle_v1_get_user_data(workspace_handle);
	zext_workspace_handle_v1_destroy(workspace_handle);

	wl_list_remove(&workspace->link);
	free(workspace->current.name);
	free(workspace->pending.name);
	wl_array_release(&workspace->pending.coordinates);
	wl_array_release(&workspace->current.coordinates);
	free(workspace);
}

static const struct zext_workspace_handle_v1_listener workspace_listener = {
	.name = workspace_handle_name,
	.coordinates = workspace_handle_coordinates,
	.state = workspace_handle_state,
	.remove = workspace_handle_remove,
};

struct group_v1 {
	struct wl_list link;

	struct zext_workspace_group_handle_v1 *handle;
	int32_t id;
	struct wl_list workspaces;
};

static void group_handle_output_enter(void *data,
		struct zext_workspace_group_handle_v1 *group_handle,
		struct wl_output *output) {
	struct group_v1 *group = (struct group_v1*)
		zext_workspace_group_handle_v1_get_user_data(group_handle);
	printf("Group %d output_enter %u\n", group->id,
		(uint32_t)(size_t)wl_output_get_user_data(output));
}

static void group_handle_output_leave(void *data,
		struct zext_workspace_group_handle_v1 *group_handle,
		struct wl_output *output) {
	struct group_v1 *group = (struct group_v1*)
		zext_workspace_group_handle_v1_get_user_data(group_handle);
	printf("Group %d output_leave %u\n", group->id,
		(uint32_t)(size_t)wl_output_get_user_data(output));
}

static void group_handle_workspace(void *data,
		struct zext_workspace_group_handle_v1 *group_handle,
		struct zext_workspace_handle_v1 *workspace_handle) {
	struct group_v1 *group = (struct group_v1*)
		zext_workspace_group_handle_v1_get_user_data(group_handle);
	struct workspace_v1 *workspace = (struct workspace_v1*)
		calloc(1, sizeof(struct workspace_v1));

	wl_list_insert(&group->workspaces, &workspace->link);
	wl_array_init(&workspace->pending.coordinates);
	wl_array_init(&workspace->current.coordinates);

	workspace->handle = workspace_handle;
	zext_workspace_handle_v1_add_listener(workspace_handle,
			&workspace_listener, NULL);
	zext_workspace_handle_v1_set_user_data(workspace_handle, workspace);
}

static void group_handle_remove(void *data,
		struct zext_workspace_group_handle_v1 *group_handle) {
	struct group_v1 *group = (struct group_v1*)
		zext_workspace_group_handle_v1_get_user_data(group_handle);
	wl_list_remove(&group->link);
	if (!wl_list_empty(&group->workspaces)) {
		printf("Compositor bug! Group destroyed before its workspaces.\n");
	}

	free(group);
}

static const struct zext_workspace_group_handle_v1_listener group_listener = {
	.output_enter = group_handle_output_enter,
	.output_leave = group_handle_output_leave,
	.workspace = group_handle_workspace,
	.remove = group_handle_remove,
};

static struct zext_workspace_manager_v1 *workspace_manager = NULL;
static struct wl_list group_list;
static int32_t last_group_id = 0;

static void workspace_manager_handle_workspace_group(void *data,
		struct zext_workspace_manager_v1 *zext_workspace_manager_v1,
		struct zext_workspace_group_handle_v1 *workspace_group) {
	struct group_v1 *group = (struct group_v1*)
		calloc(1, sizeof(struct group_v1));
	group->id = last_group_id++;
	group->handle = workspace_group;
	wl_list_init(&group->workspaces);
	wl_list_insert(&group_list, &group->link);

	zext_workspace_group_handle_v1_add_listener(workspace_group,
			&group_listener, NULL);
	zext_workspace_group_handle_v1_set_user_data(workspace_group, group);
}

static void workspace_manager_handle_done(void *data,
		struct zext_workspace_manager_v1 *zext_workspace_manager_v1) {

	printf("*** Workspace configuration ***\n");
	struct group_v1 *group;
	wl_list_for_each(group, &group_list, link) {
		printf("> Group id=%d\n", group->id);
		struct workspace_v1 *workspace;
		wl_list_for_each(workspace, &group->workspaces, link) {
			copy_state(&workspace->current, &workspace->pending);
			print_workspace(workspace);
		}
	}
}

static void workspace_manager_handle_finished(void *data,
		struct zext_workspace_manager_v1 *zext_workspace_manager_v1) {
	zext_workspace_manager_v1_destroy(zext_workspace_manager_v1);
}

static const struct zext_workspace_manager_v1_listener workspace_manager_impl = {
	.workspace_group = workspace_manager_handle_workspace_group,
	.done = workspace_manager_handle_done,
	.finished = workspace_manager_handle_finished,
};

struct wl_seat *seat = NULL;
static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *output = wl_registry_bind(registry, name,
				&wl_output_interface, version);
		wl_output_set_user_data(output, (void*)(size_t)name); // assign some ID to the output
	} else if (strcmp(interface,
			zext_workspace_manager_v1_interface.name) == 0) {
		workspace_manager = wl_registry_bind(registry, name,
				&zext_workspace_manager_v1_interface, WLR_EXT_WORKSPACE_VERSION);

		wl_list_init(&group_list);
		zext_workspace_manager_v1_add_listener(workspace_manager,
				&workspace_manager_impl, NULL);
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

static struct workspace_v1 *workspace_by_name_or_bail(const char *name) {
	struct workspace_v1 *workspace;
	struct group_v1 *group;

	wl_list_for_each(group, &group_list, link) {
		wl_list_for_each(workspace, &group->workspaces, link) {
			if (workspace->current.name &&
					strcmp(workspace->current.name, name) == 0) {
				return workspace;
			}
		}
	}

	fprintf(stderr, "No workspace with the given name: %s\n", name);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
	int c;
	char *focus_name  = NULL;
	char *create_name = NULL;
	char *remove_name = NULL;
	bool monitor = false;

	while ((c = getopt(argc, argv, "c:r:w:m")) != -1) {
		switch (c) {
		case 'c':
			create_name = strdup(optarg);
			break;
		case 'r':
			remove_name = strdup(optarg);
			break;
		case 'w':
			focus_name = strdup(optarg);
			break;
		case 'm':
			monitor = true;
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
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (workspace_manager == NULL) {
		fprintf(stderr, "wlr-workspace not available\n");
		return EXIT_FAILURE;
	}
	wl_display_roundtrip(display); // load workspace groups
	wl_display_roundtrip(display); // load details

	if (focus_name != NULL) {
		struct workspace_v1 *focus = workspace_by_name_or_bail(focus_name);

		// unfocus all workspaces
		struct workspace_v1 *workspace;
		struct group_v1 *group;

		wl_list_for_each(group, &group_list, link) {
			wl_list_for_each(workspace, &group->workspaces, link) {
				zext_workspace_handle_v1_deactivate(workspace->handle);
			}
		}
		zext_workspace_handle_v1_activate(focus->handle);
		zext_workspace_manager_v1_commit(workspace_manager);
	}

	if (create_name != NULL) {
		struct group_v1 *group;
		wl_list_for_each(group, &group_list, link) {
			zext_workspace_group_handle_v1_create_workspace(group->handle, create_name);
			break;
		}
	}

	if (remove_name != NULL) {
		struct workspace_v1 *remove = workspace_by_name_or_bail(remove_name);
		zext_workspace_handle_v1_remove(remove->handle);
	}

	wl_display_flush(display);

	if (monitor != false) {
		while (wl_display_dispatch(display) != -1) {
			// This space intentionally left blank
		}
	}

	return EXIT_SUCCESS;
}
