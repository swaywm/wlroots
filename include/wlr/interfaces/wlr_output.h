#ifndef _WLR_INTERFACE_OUTPUT_H
#define _WLR_INTERFACE_OUTPUT_H
#include <wlr/types/wlr_output.h>
#include <stdbool.h>

struct wlr_output_impl {
	void (*enable)(struct wlr_output_state *state, bool enable);
	bool (*set_mode)(struct wlr_output_state *state,
			struct wlr_output_mode *mode);
	void (*transform)(struct wlr_output_state *state,
			enum wl_output_transform transform);
	bool (*set_cursor)(struct wlr_output_state *state,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height);
	bool (*move_cursor)(struct wlr_output_state *state, int x, int y);
	void (*destroy)(struct wlr_output_state *state);
};

struct wlr_output *wlr_output_create(struct wlr_output_impl *impl,
		struct wlr_output_state *state);
void wlr_output_free(struct wlr_output *output);
void wlr_output_update_matrix(struct wlr_output *output);
struct wl_global *wlr_output_create_global(
		struct wlr_output *wlr_output, struct wl_display *display);

#endif
