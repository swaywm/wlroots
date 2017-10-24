#include <assert.h>
#include <stdlib.h>
#include <server-decoration-protocol.h>
#include <wlr/types/wlr_server_decoration.h>

static const struct org_kde_kwin_server_decoration_manager_interface
server_decoration_manager_impl = {
	// TODO
};

static void server_decoration_manager_bind(struct wl_client *client,
		void *_manager, uint32_t version, uint32_t id) {
	struct wlr_server_decoration_manager *manager = _manager;
	assert(client && manager);

	struct wl_resource *resource = wl_resource_create(client,
		&org_kde_kwin_server_decoration_manager_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &server_decoration_manager_impl,
		manager, NULL);
}

static void server_decoration_destroy(
		struct wlr_server_decoration *decoration) {
	wl_signal_emit(&decoration->events.destroy, decoration);
	wl_resource_set_user_data(decoration->resource, NULL);
	wl_list_remove(&decoration->link);
	free(decoration);
}

struct wlr_server_decoration_manager *wlr_server_decoration_manager_create(
		struct wl_display *display) {
	struct wlr_server_decoration_manager *manager =
		calloc(1, sizeof(struct wlr_server_decoration_manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->wl_global = wl_global_create(display,
		&org_kde_kwin_server_decoration_manager_interface, 1, manager,
		server_decoration_manager_bind);
	if (manager->wl_global == NULL) {
		free(manager);
		return NULL;
	}
	wl_list_init(&manager->decorations);
	return manager;
}

void wlr_server_decoration_manager_destroy(
		struct wlr_server_decoration_manager *manager) {
	if (manager == NULL) {
		return;
	}
	struct wlr_server_decoration *decoration, *tmp;
	wl_list_for_each_safe(decoration, tmp, &manager->decorations,
			link) {
		server_decoration_destroy(decoration);
	}
	wl_global_destroy(manager->wl_global);
	free(manager);
}
