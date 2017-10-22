#ifndef WLR_TYPES_WLR_DATA_SOURCE_H
#define WLR_TYPES_WLR_DATA_SOURCE_H

#include <wayland-server.h>
#include <wlr/types/wlr_list.h>

struct wlr_data_source_impl;

struct wlr_data_source {
	struct wlr_data_source_impl *impl;
	struct wlr_list *types;
	void *data;

	struct {
		struct wl_signal destroy;
	} events;
};

void wlr_data_source_send(struct wlr_data_source *src, const char *type, int fd);
void wlr_data_source_accepted(struct wlr_data_source *src, const char *type);
void wlr_data_source_cancelled(struct wlr_data_source *src);

struct wlr_wl_data_source {
	struct wlr_data_source base;
	struct wl_resource *resource;
};

struct wlr_wl_data_source *wlr_wl_data_source_create(
		struct wl_client *client,
		uint32_t version, uint32_t id);

#endif
