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

enum wlr_zext_screencopy_surface_v1_type {
	WLR_ZEXT_SCREENCOPY_SURFACE_V1_TYPE_OUTPUT,
	WLR_ZEXT_SCREENCOPY_SURFACE_V1_TYPE_OUTPUT_CURSOR,
};

enum wlr_zext_screencopy_surface_v1_state {
	WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_WAITING_FOR_BUFFER_FORMATS,
	WLR_ZEXT_SCREENCOPY_SURFACE_V1_STATE_READY,
};

struct wlr_zext_screencopy_surface_v1 {
	struct wl_resource *resource;

	enum wlr_zext_screencopy_surface_v1_type type;
	enum wlr_zext_screencopy_surface_v1_state state;

	uint32_t wl_shm_format;
	int wl_shm_stride;

	uint32_t dmabuf_format;

	/* Buffer and info staged for next commit */
	struct wl_resource *staged_buffer_resource;
	struct pixman_region32 staged_buffer_damage;
	struct wl_listener staged_buffer_destroy;

	uint32_t options;

	/* Currently attached buffer and info */
	struct wl_resource *buffer_resource;
	struct pixman_region32 buffer_damage;
	struct wl_listener buffer_destroy;

	/* Accumulated frame damage for the surface */
	struct pixman_region32 frame_damage;

	struct wlr_output *output;

	struct wl_listener output_precommit;
	struct wl_listener output_commit;
	struct wl_listener output_destroy;

	void *data;
};

struct wlr_zext_screencopy_manager_v1 *wlr_zext_screencopy_manager_v1_create(
		struct wl_display *display);

#endif
