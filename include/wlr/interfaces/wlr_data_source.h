#ifndef WLR_INTERFACES_WLR_DATA_SOURCE_H
#define WLR_INTERFACES_WLR_DATA_SOURCE_H

#include <wlr/types/wlr_data_source.h>

struct wlr_data_source_impl {
	void (*send)(struct wlr_data_source *data_source, const char *type, int fd);
	void (*accepted)(struct wlr_data_source *data_source, const char *type);
	void (*cancelled)(struct wlr_data_source *data_source);
};

bool wlr_data_source_init(struct wlr_data_source *source,
		struct wlr_data_source_impl *impl);
void wlr_data_source_finish(struct wlr_data_source *source);

#endif
