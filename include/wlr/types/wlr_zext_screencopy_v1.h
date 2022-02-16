/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_ZEXT_SCREENCOPY_V1_H
#define WLR_TYPES_WLR_ZEXT_SCREENCOPY_V1_H

#include <wayland-server-core.h>
#include <pixman.h>
#include <stdbool.h>

struct wlr_buffer;

struct wlr_zext_screencopy_manager_v1 {
	struct wl_global *global;

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

enum wlr_zext_screencopy_surface_v1_state {
	WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_WAITING_FOR_BUFFER_FORMATS = 0,
	WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_READY,
};

struct wlr_zext_screencopy_surface_v1_buffer {
	struct wl_resource *resource;
	struct pixman_region32 damage;
	struct wl_listener destroy;
};

struct wlr_zext_screencopy_surface_v1 {
	struct wl_resource *resource;

	enum wlr_zext_screencopy_surface_v1_state state;

        struct wlr_buffer *buffer;

	uint32_t wl_shm_format;
	int wl_shm_stride;
	uint32_t dmabuf_format;

	uint32_t cursor_wl_shm_format;
	int cursor_wl_shm_stride;
	uint32_t cursor_dmabuf_format;

	int cursor_width;
	int cursor_height;
	bool have_cursor;

        struct {
                int x, y, width, height;
        } last_cursor;

	uint32_t options;
	struct wlr_zext_screencopy_surface_v1_buffer staged_buffer;
	struct wlr_zext_screencopy_surface_v1_buffer current_buffer;

	struct wlr_zext_screencopy_surface_v1_buffer staged_cursor_buffer;
	struct wlr_zext_screencopy_surface_v1_buffer current_cursor_buffer;

	bool committed;

	/* Accumulated damage for the surface */
	struct pixman_region32 frame_damage;
	struct pixman_region32 cursor_damage;

	struct wlr_output *output;

	struct wl_listener output_precommit;
	struct wl_listener output_commit;
	struct wl_listener output_destroy;
	struct wl_listener output_set_cursor;
	struct wl_listener output_move_cursor;

	void *data;
};

struct wlr_zext_screencopy_manager_v1 *wlr_zext_screencopy_manager_v1_create(
		struct wl_display *display);

#endif
