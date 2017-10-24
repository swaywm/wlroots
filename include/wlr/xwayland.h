#ifndef WLR_XWAYLAND_H
#define WLR_XWAYLAND_H

#include <time.h>
#include <stdbool.h>
#include <wlr/types/wlr_compositor.h>
#include <xcb/xcb.h>
#include <wlr/types/wlr_list.h>

#ifdef HAS_XCB_ICCCM
	#include <xcb/xcb_icccm.h>
#endif

struct wlr_xwm;

struct wlr_xwayland {
	pid_t pid;
	int display;
	int x_fd[2], wl_fd[2], wm_fd[2];
	struct wl_client *client;
	struct wl_display *wl_display;
	struct wlr_compositor *compositor;
	time_t server_start;

	struct wl_event_source *sigusr1_source;
	struct wl_listener destroy_listener;
	struct wlr_xwm *xwm;

	struct {
		struct wl_signal new_surface;
	} events;

	void *data;
};

enum wlr_xwayland_surface_decorations {
	WLR_XWAYLAND_SURFACE_DECORATIONS_ALL = 0,
	WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER = 1,
	WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE = 2,
};

struct wlr_xwayland_surface_hints {
	uint32_t flags;
	uint32_t input;
	int32_t initial_state;
	xcb_pixmap_t icon_pixmap;
	xcb_window_t icon_window;
	int32_t icon_x, icon_y;
	xcb_pixmap_t icon_mask;
	xcb_window_t window_group;
};

struct wlr_xwayland_surface_size_hints {
	uint32_t flags;
	int32_t x, y;
	int32_t width, height;
	int32_t min_width, min_height;
	int32_t max_width, max_height;
	int32_t width_inc, height_inc;
	int32_t base_width, base_height;
	int32_t min_aspect_num, min_aspect_den;
	int32_t max_aspect_num, max_aspect_den;
	uint32_t win_gravity;
};

struct wlr_xwayland_surface {
	xcb_window_t window_id;
	struct wlr_xwm *xwm;
	uint32_t surface_id;

	struct wl_list link;
	struct wl_list unpaired_link;

	struct wlr_surface *surface;
	int16_t x, y;
	uint16_t width, height;
	bool override_redirect;
	bool mapped;
	bool added;

	char *title;
	char *class;
	char *instance;
	struct wlr_xwayland_surface *parent;
	struct wlr_list *state; // list of xcb_atom_t
	pid_t pid;

	xcb_atom_t *window_type;
	size_t window_type_len;

	xcb_atom_t *protocols;
	size_t protocols_len;

	uint32_t decorations;
	struct wlr_xwayland_surface_hints *hints;
	uint32_t hints_urgency;
	struct wlr_xwayland_surface_size_hints *size_hints;

	struct {
		struct wl_signal destroy;
		struct wl_signal request_configure;
		struct wl_signal set_title;
		struct wl_signal set_class;
		struct wl_signal set_parent;
		struct wl_signal set_state;
		struct wl_signal set_pid;
		struct wl_signal set_window_type;
	} events;

	struct wl_listener surface_destroy;
	struct wl_listener surface_commit;

	void *data;
};

struct wlr_xwayland_surface_configure_event {
	struct wlr_xwayland_surface *surface;
	int16_t x, y;
	uint16_t width, height;
};

void wlr_xwayland_destroy(struct wlr_xwayland *wlr_xwayland);
struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
	struct wlr_compositor *compositor);
void wlr_xwayland_surface_activate(struct wlr_xwayland *wlr_xwayland,
	struct wlr_xwayland_surface *surface);
void wlr_xwayland_surface_configure(struct wlr_xwayland *wlr_xwayland,
	struct wlr_xwayland_surface *surface, int16_t x, int16_t y,
	uint16_t width, uint16_t height);
void wlr_xwayland_surface_close(struct wlr_xwayland *wlr_xwayland,
	struct wlr_xwayland_surface *surface);

#endif
