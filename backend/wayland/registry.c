#include <stdio.h>
#include <stdint.h>
#include <wayland-client.h>
#include "backend/wayland.h"
#include "common/log.h"

static void registry_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	//struct wlr_wl_backend *backend = data;
	wlr_log(L_DEBUG, "Remote wayland global: %s v%d", interface, version);
	// TODO
}

static void registry_global_remove(void *data,
		struct wl_registry *registry, uint32_t name) {
	// TODO
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove
};

void wlr_wlb_registry_poll(struct wlr_wl_backend *backend) {
	wl_registry_add_listener(backend->remote_registry,
			&registry_listener, backend->remote_registry);
	wl_display_dispatch(backend->remote_display);
	wl_display_roundtrip(backend->remote_display);
}
