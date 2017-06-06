#ifndef _WLR_WAYLAND_INTERNAL_H
#define _WLR_WAYLAND_INTERNAL_H

#include <wayland-server.h>
#include <wlr/types.h>
#include <stdbool.h>

struct wlr_output_impl {
	void (*enable)(struct wlr_output_state *state, bool enable);
	bool (*set_mode)(struct wlr_output_state *state,
			struct wlr_output_mode *mode);
	void (*transform)(struct wlr_output_state *state,
			enum wl_output_transform transform);
	void (*destroy)(struct wlr_output_state *state);
};

struct wlr_output *wlr_output_create(struct wlr_output_impl *impl,
		struct wlr_output_state *state);
void wlr_output_free(struct wlr_output *output);

#endif
