#define _XOPEN_SOURCE 700
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/util/list.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_data_source.h>
#include <wlr/types/wlr_data_device_manager.h>
#include <wlr/interfaces/wlr_data_source.h>

bool wlr_data_source_init(struct wlr_data_source *source,
		struct wlr_data_source_impl *impl) {
	source->impl = impl;
	wl_signal_init(&source->events.destroy);
	return (source->types = list_create());
}

void wlr_data_source_finish(struct wlr_data_source *source) {
	if (source) {
		wl_signal_emit(&source->events.destroy, source);
		if (source->types) {
			list_foreach(source->types, free);
		}
		list_free(source->types);
	}
}

void wlr_data_source_send(struct wlr_data_source *src, const char *type, int fd) {
	assert(src && src->impl && src->impl->send);
	src->impl->send(src, type, fd);
}

void wlr_data_source_accepted(struct wlr_data_source *src, const char *type) {
	assert(src && src->impl);
	if (src->impl->accepted) {
		src->impl->accepted(src, type);
	}
}

void wlr_data_source_cancelled(struct wlr_data_source *src) {
	assert(src && src->impl);
	if (src->impl->cancelled) {
		src->impl->cancelled(src);
	}
}

static void data_source_send(struct wlr_data_source *src,
		const char *type, int fd) {
	struct wlr_wl_data_source *wl_src = (struct wlr_wl_data_source *) src;
	wl_data_source_send_send(wl_src->resource, type, fd);
	close(fd);
}

static void data_source_accepted(struct wlr_data_source *src, const char *type) {
	struct wlr_wl_data_source *wl_src = (struct wlr_wl_data_source *) src;
	wl_data_source_send_target(wl_src->resource, type);
}

static void data_source_cancelled(struct wlr_data_source *src) {
	struct wlr_wl_data_source *wl_src = (struct wlr_wl_data_source *) src;
	wl_data_source_send_cancelled(wl_src->resource);
}

static struct wlr_data_source_impl data_source_wl_impl = {
	.send = data_source_send,
	.accepted = data_source_accepted,
	.cancelled = data_source_cancelled,
};

static void data_source_offer(struct wl_client *client, struct wl_resource *resource,
		const char *type) {
	struct wlr_wl_data_source *src = wl_resource_get_user_data(resource);
	char *dtype = strdup(type);
	if (!dtype) {
		wl_resource_post_no_memory(resource);
		return;
	}

	list_add(src->base.types, dtype);
}

static void data_source_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void data_source_set_actions(struct wl_client *client,
		struct wl_resource *resource, uint32_t dnd_actions) {
	wlr_log(L_DEBUG, "TODO: data source set actions");
}

static struct wl_data_source_interface wl_data_source_impl = {
	.offer = data_source_offer,
	.destroy = data_source_destroy,
	.set_actions = data_source_set_actions,
};

static void destroy_wl_data_source(struct wl_resource *resource) {
	struct wlr_wl_data_source *src = wl_resource_get_user_data(resource);
	wlr_data_source_finish(&src->base);
	free(src);
}

struct wlr_wl_data_source *wlr_wl_data_source_create(
		struct wl_client *client,
		uint32_t version, uint32_t id) {
	struct wlr_wl_data_source *src = calloc(1, sizeof(*src));
	if (!src) {
		wlr_log(L_ERROR, "Failed to allocator wlr_wl_data_source");
		wl_client_post_no_memory(client);
		return NULL;
	}

	if (!wlr_data_source_init(&src->base, &data_source_wl_impl)) {
		wlr_log(L_ERROR, "Failed to init wlr_wl_data_source");
		wl_client_post_no_memory(client);
		goto err;
	}

	if (!(src->resource = wl_resource_create(client, &wl_data_source_interface,
			version, id))) {
		wlr_log(L_ERROR, "Failed to create wl_resource for wlr_wl_data_source");
		wl_client_post_no_memory(client);
		goto err;
	}

	wl_resource_set_implementation(src->resource, &wl_data_source_impl, src,
		destroy_wl_data_source);
	return src;

err:
	wlr_data_source_finish(&src->base);
	free(src);
	return NULL;
}
