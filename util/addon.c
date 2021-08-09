#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>

#include "wlr/util/addon.h"

void wlr_addon_set_init(struct wlr_addon_set *set) {
	wl_list_init(&set->addons);
}

void wlr_addon_set_finish(struct wlr_addon_set *set) {
	struct wlr_addon *addon, *tmp;
	wl_list_for_each_safe(addon, tmp, &set->addons, link) {
		wlr_addon_finish(addon);
		addon->impl->destroy(addon);
	}
}

void wlr_addon_init(struct wlr_addon *addon, struct wlr_addon_set *set,
		const void *owner, const struct wlr_addon_interface *impl) {
	assert(owner);
	struct wlr_addon *iter;
	wl_list_for_each(iter, &set->addons, link) {
		if (iter->owner == addon->owner) {
			assert(0 && "Can't have two addons with the same owner");
		}
	}
	wl_list_insert(&set->addons, &addon->link);
	addon->owner = owner;
	addon->impl = impl;
}

void wlr_addon_finish(struct wlr_addon *addon) {
	if (addon->owner) {
		addon->owner = NULL;
		wl_list_remove(&addon->link);
	}
}

struct wlr_addon *wlr_addon_find_by_owner(struct wlr_addon_set *set,
		const void *owner) {
	struct wlr_addon *addon;
	wl_list_for_each(addon, &set->addons, link) {
		if (addon->owner == owner) {
			return addon;
		}
	}
	return NULL;
}
