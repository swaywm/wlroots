#ifndef _WLR_INTERFACES_TABLET_PAD_H
#define _WLR_INTERFACES_TABLET_PAD_H
#include <wlr/types/wlr_tablet_pad.h>

struct wlr_tablet_pad_impl {
	void (*destroy)(struct wlr_tablet_pad_state *pad);
};

struct wlr_tablet_pad *wlr_tablet_pad_create(struct wlr_tablet_pad_impl *impl,
		struct wlr_tablet_pad_state *state);
void wlr_tablet_pad_destroy(struct wlr_tablet_pad *pad);

#endif
