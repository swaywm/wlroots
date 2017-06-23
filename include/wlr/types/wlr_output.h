#ifndef _WLR_TYPES_OUTPUT_H
#define _WLR_TYPES_OUTPUT_H
#include <wayland-server.h>
#include <wlr/util/list.h>
#include <stdbool.h>

struct wlr_output_mode_state;

struct wlr_output_mode {
	struct wlr_output_mode_state *state;
	uint32_t flags; // enum wl_output_mode
	int32_t width, height;
	int32_t refresh; // mHz
};

struct wlr_output_impl;
struct wlr_output_state;

struct wlr_output {
	const struct wlr_output_impl *impl;
	struct wlr_output_state *state;
	void *user_data;
	struct wl_global *wl_global;
	struct wl_list wl_resources;

	uint32_t flags;
	char name[16];
	char make[48];
	char model[16];
	uint32_t scale;
	int32_t width, height;
	int32_t phys_width, phys_height; // mm
	int32_t subpixel; // enum wl_output_subpixel
	int32_t transform; // enum wl_output_transform

	float transform_matrix[16];

	/* Note: some backends may have zero modes */
	list_t *modes;
	struct wlr_output_mode *current_mode;

	struct {
		struct wl_signal frame;
		struct wl_signal resolution;
	} events;
};

void wlr_output_enable(struct wlr_output *output, bool enable);
bool wlr_output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode);
void wlr_output_transform(struct wlr_output *output,
		enum wl_output_transform transform);
bool wlr_output_set_cursor(struct wlr_output *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height);
bool wlr_output_move_cursor(struct wlr_output *output, int x, int y);
void wlr_output_destroy(struct wlr_output *output);
void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height);

#endif
