#ifndef _WLR_INTERFACES_TABLET_PAD_H
#define _WLR_INTERFACES_TABLET_PAD_H
#include <wlr/types/wlr_tablet_pad.h>

struct wlr_tablet_pad_impl {
	void (*destroy)(struct wlr_tablet_pad *pad);
};

void wlr_tablet_pad_init(struct wlr_tablet_pad *pad,
		struct wlr_tablet_pad_impl *impl);
void wlr_tablet_pad_destroy(struct wlr_tablet_pad *pad);

#endif
