#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <wayland-server.h>
#include <wlr/wlcore/wl_shm.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include <wlr/render.h>

struct wlr_wl_shm {
	struct wl_global *wl_global;
	struct wl_list resources;
	struct wl_list pools;
	list_t *formats;
};

static void wl_shm_destroy(struct wl_resource *resource) {
	struct wlr_wl_shm *shm = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &shm->resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

struct wl_shm_interface wl_shm_impl = {
	//.create_pool = wl_shm_create_pool
};

static void wl_shm_bind(struct wl_client *wl_client, void *_wlr_wl_shm,
		uint32_t version, uint32_t id) {
	struct wlr_wl_shm *wlr_shm = _wlr_wl_shm;
	assert(wl_client && wlr_shm);
	if (version > 1) {
		wlr_log(L_ERROR, "Client requested unsupported wl_shm version, disconnecting");
		wl_client_destroy(wl_client);
		return;
	}
	struct wl_resource *wl_resource = wl_resource_create(
			wl_client, &wl_shm_interface, version, id);
	wl_resource_set_implementation(wl_resource, &wl_shm_impl,
			wlr_shm, wl_shm_destroy);
	wl_list_insert(&wlr_shm->resources, wl_resource_get_link(wl_resource));
	for (size_t i = 0; i < wlr_shm->formats->length; ++i) {
		uint32_t *f = wlr_shm->formats->items[i];
		wl_shm_send_format(wl_resource, *f);
	}
}

struct wlr_wl_shm *wlr_wl_shm_init(struct wl_display *display) {
	struct wlr_wl_shm *shm = calloc(1, sizeof(struct wlr_wl_shm));
	wl_list_init(&shm->resources);
	wl_list_init(&shm->pools);
	shm->formats = list_create();
	shm->wl_global = wl_global_create(display, &wl_shm_interface, 1,
			shm, wl_shm_bind);
	return shm;
}

void wlr_wl_shm_add_format(struct wlr_wl_shm *shm, enum wl_shm_format format) {
	assert(shm);
	uint32_t *f = calloc(1, sizeof(uint32_t));
	*f = format;
	list_add(shm->formats, f);
}

void wlr_wl_shm_add_renderer_formats(struct wlr_wl_shm *shm,
		struct wlr_renderer *renderer) {
	assert(shm && renderer);
	size_t len;
	const enum wl_shm_format *formats = wlr_renderer_get_formats(renderer, &len);
	for (size_t i = 0; i < len; ++i) {
		wlr_wl_shm_add_format(shm, formats[i]);
	}
}
